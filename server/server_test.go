package main

import (
	"context"
	"path/filepath"
	"testing"
	"time"
)

func TestParseOrcaShapes(t *testing.T) {
	cases := map[string]string{
		"array":         `[{"orca_session_id":"s1","name":"fix-auth","agent":"claude","status":"waiting"}]`,
		"wrapped":       `{"agents":[{"id":"s1","worktree":"/repo/wt/fix-auth","type":"claude","state":"waiting"}]}`,
		"status object": `[{"sessionId":"s1","title":"fix-auth","agentType":"claude","status":{"state":"waiting"}}]`,
	}
	for name, in := range cases {
		got, err := parseOrca([]byte(in))
		if err != nil || len(got) != 1 {
			t.Fatalf("%s: got %v, %v", name, got, err)
		}
		a := got[0]
		if a.ID != "s1" || a.Name != "fix-auth" || a.Kind != "claude" || a.State != "waiting" {
			t.Errorf("%s: unexpected agent %+v", name, a)
		}
	}
}

func TestNormalizeState(t *testing.T) {
	for in, want := range map[[2]string]string{
		{"working", ""}: "working", {"blocked", ""}: "blocked", {"waiting", ""}: "waiting",
		{"done", ""}: "done", {"done", "failure"}: "failed", {"done", "interrupted"}: "done",
	} {
		if got := normalizeState(in[0], in[1]); got != want {
			t.Errorf("normalizeState(%q, %q) = %q, want %q", in[0], in[1], got, want)
		}
	}
}

func TestTrackerKeepsSinceUntilStateChanges(t *testing.T) {
	f := &FakeSource{agents: map[string]Agent{}}
	f.Set("a", "claude", "working")
	tr := NewTracker(f)
	clock := time.Unix(1000, 0)
	tr.now = func() time.Time { return clock }

	if !tr.Poll(context.Background()) {
		t.Fatal("first poll should report a change")
	}
	clock = clock.Add(time.Minute)
	if tr.Poll(context.Background()) {
		t.Fatal("nothing changed on the second poll")
	}
	if a, _ := tr.Agents(); a[0].Since != 1000 {
		t.Fatalf("since moved without a state change: %d", a[0].Since)
	}
	f.Set("a", "claude", "waiting")
	if !tr.Poll(context.Background()) {
		t.Fatal("state change not reported")
	}
	if a, _ := tr.Agents(); a[0].Since != 1060 || a[0].State != "waiting" {
		t.Fatalf("unexpected agent after change: %+v", a[0])
	}
}

func TestStoreGoalsSessionsAndCarryOver(t *testing.T) {
	path := filepath.Join(t.TempDir(), "state.json")
	s, err := OpenStore(path)
	if err != nil {
		t.Fatal(err)
	}
	day := time.Date(2026, 11, 23, 10, 0, 0, 0, time.Local)
	s.now = func() time.Time { return day }

	a, _ := s.AddGoal("write roadmap", 2)
	b, _ := s.AddGoal("book tickets", 0)
	done := true
	if err := s.UpdateGoal(b.ID, GoalPatch{Done: &done}); err != nil {
		t.Fatal(err)
	}
	if err := s.AddSession("focus", a.ID, 25); err != nil {
		t.Fatal(err)
	}
	if err := s.Reorder([]string{b.ID, a.ID}); err != nil {
		t.Fatal(err)
	}
	snap := s.Snapshot()
	if len(snap.Goals) != 2 || snap.Goals[0].ID != b.ID || snap.Goals[1].Pomos != 1 || snap.PomosToday != 1 || snap.FocusMin != 25 {
		t.Fatalf("unexpected snapshot: %+v", snap)
	}

	// Reload from disk, next day: the list starts empty and offers the unfinished goal.
	s2, err := OpenStore(path)
	if err != nil {
		t.Fatal(err)
	}
	s2.now = func() time.Time { return day.Add(24 * time.Hour) }
	snap = s2.Snapshot()
	if len(snap.Goals) != 0 || snap.Carry != 1 || snap.PomosToday != 0 {
		t.Fatalf("unexpected next-day snapshot: %+v", snap)
	}
	if n, _ := s2.CarryOver(); n != 1 {
		t.Fatalf("carried %d goals, want 1", n)
	}
	snap = s2.Snapshot()
	if len(snap.Goals) != 1 || snap.Goals[0].Title != "write roadmap" || snap.Goals[0].Pomos != 0 || snap.Goals[0].Est != 2 {
		t.Fatalf("unexpected carried goal: %+v", snap.Goals)
	}
}

package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os/exec"
	"sort"
	"strings"
	"sync"
	"time"
)

// Agent is one coding agent as shown on the Presto.
type Agent struct {
	ID    string `json:"id"`
	Name  string `json:"name"`
	Kind  string `json:"kind"`
	State string `json:"state"` // working | waiting | blocked | done | failed
	Since int64  `json:"since"` // unix seconds when it entered this state
}

// AgentSource lists the agents currently known.
type AgentSource interface {
	List(ctx context.Context) ([]Agent, error)
}

// ---- Orca ----

// OrcaSource shells out to `orca agent list --json`.
type OrcaSource struct{ Command string }

var errNoOrca = errors.New("Orca CLI not found. In Orca: Settings > Experimental > CLI.")

func (o OrcaSource) List(ctx context.Context) ([]Agent, error) {
	path, err := exec.LookPath(o.Command)
	if err != nil {
		return nil, errNoOrca
	}
	out, err := exec.CommandContext(ctx, path, "agent", "list", "--json").Output()
	if err != nil {
		return nil, fmt.Errorf("orca agent list failed: %w", err)
	}
	return parseOrca(out)
}

// parseOrca reads the CLI's JSON. Orca does not document the field names,
// so this accepts the plausible spellings; it must be checked against real
// output (see testdata/) once the CLI is available on this machine.
func parseOrca(out []byte) ([]Agent, error) {
	var raw any
	if err := json.Unmarshal(out, &raw); err != nil {
		return nil, fmt.Errorf("orca output is not JSON: %w", err)
	}
	var items []any
	switch v := raw.(type) {
	case []any:
		items = v
	case map[string]any:
		// Orca wraps its output as {"ok":true,"result":{"agents":[...]}}.
		if res, ok := v["result"].(map[string]any); ok {
			v = res
		}
		for _, key := range []string{"agents", "sessions", "items", "data"} {
			if arr, ok := v[key].([]any); ok {
				items = arr
				break
			}
		}
	}
	agents := []Agent{}
	for _, it := range items {
		obj, ok := it.(map[string]any)
		if !ok {
			continue
		}
		a := Agent{
			ID:   firstString(obj, "orca_session_id", "orcaSessionId", "session_id", "sessionId", "id"),
			Name: firstString(obj, "name", "title", "label", "worktree", "worktree_name", "worktreeName", "branch"),
			Kind: firstString(obj, "agent", "agent_type", "agentType", "kind", "type", "provider"),
		}
		state, outcome := "", firstString(obj, "outcome")
		switch st := firstValue(obj, "status", "state", "agent_status", "agentStatus").(type) {
		case string:
			state = st
		case map[string]any:
			state = firstString(st, "state", "status")
			if o := firstString(st, "outcome"); o != "" {
				outcome = o
			}
		}
		a.State = normalizeState(state, outcome)
		if a.ID == "" {
			a.ID = a.Name
		}
		if a.ID == "" {
			continue
		}
		if a.Name == "" {
			a.Name = a.ID
		}
		// Worktree paths make poor names on a small screen.
		if i := strings.LastIndex(a.Name, "/"); i >= 0 && i < len(a.Name)-1 {
			a.Name = a.Name[i+1:]
		}
		agents = append(agents, a)
	}
	return agents, nil
}

func normalizeState(state, outcome string) string {
	state, outcome = strings.ToLower(state), strings.ToLower(outcome)
	switch state {
	case "working", "running", "busy", "active":
		return "working"
	case "waiting", "idle", "needs_input", "needs-input", "awaiting_input":
		return "waiting"
	case "blocked", "permission", "needs_permission":
		return "blocked"
	case "done", "finished", "completed", "complete":
		if outcome == "failure" || outcome == "failed" || outcome == "error" {
			return "failed"
		}
		return "done"
	case "failed", "error", "errored":
		return "failed"
	}
	return "working"
}

func firstValue(obj map[string]any, keys ...string) any {
	for _, k := range keys {
		if v, ok := obj[k]; ok && v != nil {
			return v
		}
	}
	return nil
}

func firstString(obj map[string]any, keys ...string) string {
	if s, ok := firstValue(obj, keys...).(string); ok {
		return s
	}
	return ""
}

// ---- fake agents, for development without Orca ----

// FakeSource holds agents that are set by hand through the debug API.
type FakeSource struct {
	mu     sync.Mutex
	agents map[string]Agent
}

func NewFakeSource() *FakeSource {
	f := &FakeSource{agents: map[string]Agent{}}
	f.Set("fix-auth-refresh", "claude", "working")
	f.Set("docs-rewrite", "codex", "working")
	return f
}

func (f *FakeSource) Set(name, kind, state string) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if state == "gone" {
		delete(f.agents, name)
		return
	}
	if kind == "" {
		kind = "claude"
	}
	f.agents[name] = Agent{ID: name, Name: name, Kind: kind, State: normalizeState(state, "")}
}

func (f *FakeSource) List(context.Context) ([]Agent, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	out := []Agent{}
	for _, a := range f.agents {
		out = append(out, a)
	}
	return out, nil
}

// ---- tracker ----

// Tracker polls a source and remembers when each agent entered its state.
type Tracker struct {
	src  AgentSource
	mu   sync.Mutex
	cur  []Agent
	note string
	now  func() time.Time
}

func NewTracker(src AgentSource) *Tracker { return &Tracker{src: src, now: time.Now, cur: []Agent{}} }

// Poll refreshes the list and reports whether anything changed.
func (t *Tracker) Poll(ctx context.Context) bool {
	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()
	list, err := t.src.List(ctx)

	t.mu.Lock()
	defer t.mu.Unlock()
	note := ""
	if err != nil {
		note = err.Error()
		list = nil
	}
	prev := map[string]Agent{}
	for _, a := range t.cur {
		prev[a.ID] = a
	}
	next := make([]Agent, 0, len(list))
	for _, a := range list {
		if old, ok := prev[a.ID]; ok && old.State == a.State {
			a.Since = old.Since
		} else {
			a.Since = t.now().Unix()
		}
		next = append(next, a)
	}
	sort.Slice(next, func(i, j int) bool { return next[i].ID < next[j].ID })
	if len(next) > 24 {
		next = next[:24]
	}
	changed := note != t.note || len(next) != len(t.cur)
	for i := 0; !changed && i < len(next); i++ {
		changed = next[i] != t.cur[i]
	}
	t.cur, t.note = next, note
	return changed
}

func (t *Tracker) Agents() ([]Agent, string) {
	t.mu.Lock()
	defer t.mu.Unlock()
	return append([]Agent{}, t.cur...), t.note
}

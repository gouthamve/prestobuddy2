package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"
)

// Goal is one thing to get done today.
type Goal struct {
	ID    string `json:"id"`
	Title string `json:"title"`
	Done  bool   `json:"done"`
	Est   int    `json:"est"`   // estimated focus sessions, 0 = none
	Pomos int    `json:"pomos"` // completed focus sessions
}

// Session is a finished focus or breathing session.
type Session struct {
	Kind    string    `json:"kind"` // "focus" or "breathe"
	Goal    string    `json:"goal,omitempty"`
	Minutes int       `json:"minutes"`
	At      time.Time `json:"at"`
}

// Day holds everything recorded for one calendar date.
type Day struct {
	Goals    []*Goal   `json:"goals"`
	Sessions []Session `json:"sessions,omitempty"`
}

// Settings are mirrored to the Presto.
type Settings struct {
	FocusMin    int    `json:"focus_min"`
	ShortMin    int    `json:"short_min"`
	LongMin     int    `json:"long_min"`
	LongEvery   int    `json:"long_every"`
	LED         int    `json:"led"`
	Sound       bool   `json:"sound"`
	Volume      int    `json:"volume"`
	Quiet       bool   `json:"quiet"`
	QuietFrom   int    `json:"quiet_from"`
	QuietTo     int    `json:"quiet_to"`
	FocusAlerts string `json:"focus_alerts"` // off | soft | full
	Name        string `json:"name"`
}

func defaultSettings() Settings {
	return Settings{FocusMin: 25, ShortMin: 5, LongMin: 15, LongEvery: 4, LED: 60, Sound: true, Volume: 50,
		QuietFrom: 22, QuietTo: 7, FocusAlerts: "soft", Name: "Garder"}
}

func (s *Settings) sanitize() {
	clamp := func(v *int, lo, hi int) {
		if *v < lo {
			*v = lo
		}
		if *v > hi {
			*v = hi
		}
	}
	clamp(&s.FocusMin, 1, 180)
	clamp(&s.ShortMin, 1, 60)
	clamp(&s.LongMin, 1, 90)
	clamp(&s.LongEvery, 2, 10)
	clamp(&s.LED, 0, 100)
	clamp(&s.Volume, 0, 100)
	clamp(&s.QuietFrom, 0, 23)
	clamp(&s.QuietTo, 0, 23)
	if s.FocusAlerts != "off" && s.FocusAlerts != "full" {
		s.FocusAlerts = "soft"
	}
	if s.Name == "" {
		s.Name = "Garder"
	}
	if len(s.Name) > 20 {
		s.Name = s.Name[:20]
	}
}

type fileData struct {
	Days     map[string]*Day `json:"days"`
	Settings Settings        `json:"settings"`
	NextID   int             `json:"next_id"`
}

// Store is the persistent state, saved as one JSON file.
type Store struct {
	mu   sync.Mutex
	path string
	data fileData
	now  func() time.Time
}

func OpenStore(path string) (*Store, error) {
	s := &Store{path: path, now: time.Now}
	s.data = fileData{Days: map[string]*Day{}, Settings: defaultSettings(), NextID: 1}
	b, err := os.ReadFile(path)
	if err == nil {
		if err := json.Unmarshal(b, &s.data); err != nil {
			return nil, fmt.Errorf("reading %s: %w", path, err)
		}
	} else if !os.IsNotExist(err) {
		return nil, err
	}
	if s.data.Days == nil {
		s.data.Days = map[string]*Day{}
	}
	s.data.Settings.sanitize()
	return s, nil
}

func (s *Store) save() error {
	if s.path == "" {
		return nil
	}
	b, err := json.MarshalIndent(&s.data, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(s.path), 0o755); err != nil {
		return err
	}
	tmp := s.path + ".tmp"
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, s.path)
}

func (s *Store) todayKey() string { return s.now().Format("2006-01-02") }

func (s *Store) today() *Day {
	k := s.todayKey()
	d := s.data.Days[k]
	if d == nil {
		d = &Day{}
		s.data.Days[k] = d
	}
	return d
}

func (s *Store) find(id string) *Goal {
	for _, g := range s.today().Goals {
		if g.ID == id {
			return g
		}
	}
	return nil
}

// Snapshot is a copy of what the UI and the Presto need.
type Snapshot struct {
	Date       string
	Goals      []Goal
	Settings   Settings
	PomosToday int
	FocusMin   int // minutes focused today
	Carry      int // unfinished goals left on the most recent earlier day
}

func (s *Store) Snapshot() Snapshot {
	s.mu.Lock()
	defer s.mu.Unlock()
	d := s.today()
	snap := Snapshot{Date: s.todayKey(), Settings: s.data.Settings, Goals: []Goal{}}
	for _, g := range d.Goals {
		snap.Goals = append(snap.Goals, *g)
	}
	for _, se := range d.Sessions {
		if se.Kind == "focus" {
			snap.PomosToday++
			snap.FocusMin += se.Minutes
		}
	}
	if len(d.Goals) == 0 {
		if prev := s.previousDay(); prev != nil {
			for _, g := range prev.Goals {
				if !g.Done {
					snap.Carry++
				}
			}
		}
	}
	return snap
}

func (s *Store) previousDay() *Day {
	today := s.todayKey()
	keys := make([]string, 0, len(s.data.Days))
	for k, d := range s.data.Days {
		if k < today && len(d.Goals) > 0 {
			keys = append(keys, k)
		}
	}
	if len(keys) == 0 {
		return nil
	}
	sort.Strings(keys)
	return s.data.Days[keys[len(keys)-1]]
}

func (s *Store) AddGoal(title string, est int) (Goal, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if title == "" {
		return Goal{}, fmt.Errorf("a goal needs a title")
	}
	if len(s.today().Goals) >= 24 {
		return Goal{}, fmt.Errorf("24 goals is the most the Presto can show")
	}
	g := &Goal{ID: fmt.Sprintf("g%d", s.data.NextID), Title: title, Est: est}
	s.data.NextID++
	d := s.today()
	d.Goals = append(d.Goals, g)
	return *g, s.save()
}

// GoalPatch changes any subset of a goal's fields.
type GoalPatch struct {
	Title *string `json:"title"`
	Done  *bool   `json:"done"`
	Est   *int    `json:"est"`
}

func (s *Store) UpdateGoal(id string, p GoalPatch) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	g := s.find(id)
	if g == nil {
		return fmt.Errorf("no goal %q today", id)
	}
	if p.Title != nil && *p.Title != "" {
		g.Title = *p.Title
	}
	if p.Done != nil {
		g.Done = *p.Done
	}
	if p.Est != nil && *p.Est >= 0 && *p.Est <= 12 {
		g.Est = *p.Est
	}
	return s.save()
}

func (s *Store) DeleteGoal(id string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	d := s.today()
	for i, g := range d.Goals {
		if g.ID == id {
			d.Goals = append(d.Goals[:i], d.Goals[i+1:]...)
			return s.save()
		}
	}
	return fmt.Errorf("no goal %q today", id)
}

// Reorder puts today's goals in the given order; unknown ids are ignored
// and goals not mentioned keep their relative order at the end.
func (s *Store) Reorder(ids []string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	d := s.today()
	seen := map[string]bool{}
	var out []*Goal
	for _, id := range ids {
		if g := s.find(id); g != nil && !seen[id] {
			out = append(out, g)
			seen[id] = true
		}
	}
	for _, g := range d.Goals {
		if !seen[g.ID] {
			out = append(out, g)
		}
	}
	d.Goals = out
	return s.save()
}

// CarryOver copies unfinished goals from the most recent earlier day.
func (s *Store) CarryOver() (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	prev := s.previousDay()
	if prev == nil {
		return 0, nil
	}
	d := s.today()
	n := 0
	for _, g := range prev.Goals {
		if g.Done || len(d.Goals) >= 24 {
			continue
		}
		d.Goals = append(d.Goals, &Goal{ID: fmt.Sprintf("g%d", s.data.NextID), Title: g.Title, Est: g.Est})
		s.data.NextID++
		n++
	}
	return n, s.save()
}

func (s *Store) AddSession(kind, goal string, minutes int) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	d := s.today()
	d.Sessions = append(d.Sessions, Session{Kind: kind, Goal: goal, Minutes: minutes, At: s.now()})
	if kind == "focus" {
		if g := s.find(goal); g != nil {
			g.Pomos++
		}
	}
	return s.save()
}

func (s *Store) SetSettings(n Settings) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	n.sanitize()
	s.data.Settings = n
	return s.save()
}

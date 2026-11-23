// buddyd is the laptop half of prestobuddy2: it stores the day's goals,
// serves the web page for typing them, tracks coding agents and keeps the
// Presto in sync over WiFi.
package main

import (
	"context"
	"embed"
	"encoding/json"
	"flag"
	"fmt"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

//go:embed web
var webFS embed.FS

func main() {
	httpAddr := flag.String("http", "127.0.0.1:8787", "address for the web page")
	devicePort := flag.Int("device-port", 47801, "TCP port the Presto connects to")
	beaconPort := flag.Int("beacon-port", 47800, "UDP port the discovery beacon is sent to")
	dataPath := flag.String("data", "", "state file (default: ~/Library/Application Support/prestobuddy/state.json)")
	orcaCmd := flag.String("orca", "orca", "Orca CLI command")
	fakeAgents := flag.Bool("fake-agents", false, "use hand-driven fake agents instead of Orca")
	flag.Parse()

	if *dataPath == "" {
		dir, err := os.UserConfigDir()
		if err != nil {
			log.Fatal(err)
		}
		*dataPath = filepath.Join(dir, "prestobuddy", "state.json")
	}
	store, err := OpenStore(*dataPath)
	if err != nil {
		log.Fatal(err)
	}

	var src AgentSource = OrcaSource{Command: *orcaCmd}
	var fake *FakeSource
	if *fakeAgents {
		fake = NewFakeSource()
		src = fake
	}
	tracker := NewTracker(src)
	hub := NewHub(store, tracker, fake)

	ln, err := net.Listen("tcp4", fmt.Sprintf(":%d", *devicePort))
	if err != nil {
		log.Fatalf("device port: %v", err)
	}
	go hub.ServeDevices(ln)
	go Beacon(*beaconPort, *devicePort)

	go func() { // agents, and the midnight rollover
		day := store.Snapshot().Date
		for {
			changed := tracker.Poll(context.Background())
			if d := store.Snapshot().Date; d != day {
				day, changed = d, true
			}
			if changed {
				hub.Changed()
			}
			time.Sleep(2 * time.Second)
		}
	}()

	log.Printf("state file: %s", *dataPath)
	log.Printf("goals page: http://%s", *httpAddr)
	log.Fatal(http.ListenAndServe(*httpAddr, routes(hub)))
}

func routes(h *Hub) http.Handler {
	mux := http.NewServeMux()
	static, _ := fs.Sub(webFS, "web")
	mux.Handle("GET /", http.FileServer(http.FS(static)))

	reply := func(w http.ResponseWriter, err error) {
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		h.Changed()
		w.WriteHeader(http.StatusNoContent)
	}
	decode := func(w http.ResponseWriter, r *http.Request, v any) bool {
		if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 64<<10)).Decode(v); err != nil {
			http.Error(w, "bad JSON: "+err.Error(), http.StatusBadRequest)
			return false
		}
		return true
	}

	mux.HandleFunc("GET /api/state", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(h.WebState())
	})

	mux.HandleFunc("GET /api/events", func(w http.ResponseWriter, r *http.Request) {
		fl, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "streaming unsupported", http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-cache")
		ch := h.Subscribe()
		defer h.Unsubscribe(ch)
		first, _ := json.Marshal(h.WebState())
		fmt.Fprintf(w, "data: %s\n\n", first)
		fl.Flush()
		keep := time.NewTicker(20 * time.Second)
		defer keep.Stop()
		for {
			select {
			case <-r.Context().Done():
				return
			case b := <-ch:
				fmt.Fprintf(w, "data: %s\n\n", b)
			case <-keep.C:
				fmt.Fprint(w, ": keepalive\n\n")
			}
			fl.Flush()
		}
	})

	mux.HandleFunc("POST /api/goals", func(w http.ResponseWriter, r *http.Request) {
		var in struct {
			Title string `json:"title"`
			Est   int    `json:"est"`
		}
		if !decode(w, r, &in) {
			return
		}
		_, err := h.store.AddGoal(strings.TrimSpace(in.Title), in.Est)
		reply(w, err)
	})
	mux.HandleFunc("PATCH /api/goals/{id}", func(w http.ResponseWriter, r *http.Request) {
		var p GoalPatch
		if !decode(w, r, &p) {
			return
		}
		if p.Title != nil {
			t := strings.TrimSpace(*p.Title)
			p.Title = &t
		}
		reply(w, h.store.UpdateGoal(r.PathValue("id"), p))
	})
	mux.HandleFunc("DELETE /api/goals/{id}", func(w http.ResponseWriter, r *http.Request) {
		reply(w, h.store.DeleteGoal(r.PathValue("id")))
	})
	mux.HandleFunc("POST /api/reorder", func(w http.ResponseWriter, r *http.Request) {
		var in struct {
			IDs []string `json:"ids"`
		}
		if !decode(w, r, &in) {
			return
		}
		reply(w, h.store.Reorder(in.IDs))
	})
	mux.HandleFunc("POST /api/carryover", func(w http.ResponseWriter, r *http.Request) {
		_, err := h.store.CarryOver()
		reply(w, err)
	})
	mux.HandleFunc("PUT /api/settings", func(w http.ResponseWriter, r *http.Request) {
		s := h.store.Snapshot().Settings
		if !decode(w, r, &s) {
			return
		}
		reply(w, h.store.SetSettings(s))
	})
	mux.HandleFunc("POST /api/timer", func(w http.ResponseWriter, r *http.Request) {
		var in struct {
			Action string `json:"action"` // focus | break | pause | stop
			Goal   string `json:"goal"`
			Min    int    `json:"min"`
		}
		if !decode(w, r, &in) {
			return
		}
		cmd := map[string]any{}
		switch in.Action {
		case "focus":
			cmd = map[string]any{"cmd": "focus_start", "goal": in.Goal, "min": in.Min}
		case "break":
			cmd = map[string]any{"cmd": "break_start", "min": in.Min}
		case "pause":
			cmd = map[string]any{"cmd": "pause"}
		case "stop":
			cmd = map[string]any{"cmd": "stop"}
		case "page": // show a screen on the Presto; min is the page index
			cmd = map[string]any{"cmd": "page", "n": in.Min}
		default:
			http.Error(w, "unknown action", http.StatusBadRequest)
			return
		}
		if !h.Command(cmd) {
			http.Error(w, "the Presto is not connected", http.StatusServiceUnavailable)
			return
		}
		w.WriteHeader(http.StatusNoContent)
	})
	// Only with -fake-agents: drive an agent's state by hand.
	mux.HandleFunc("POST /api/fake-agent", func(w http.ResponseWriter, r *http.Request) {
		if h.fake == nil {
			http.Error(w, "start buddyd with -fake-agents", http.StatusNotFound)
			return
		}
		var in struct{ Name, Kind, State string }
		if !decode(w, r, &in) {
			return
		}
		h.fake.Set(in.Name, in.Kind, in.State)
		h.tracker.Poll(r.Context())
		reply(w, nil)
	})
	return mux
}

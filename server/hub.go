package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"sync"
	"time"
)

// DeviceStatus is the Presto's timer state as it last reported it.
type DeviceStatus struct {
	Connected  bool   `json:"connected"`
	Phase      string `json:"phase"` // idle | focus | done | break
	Paused     bool   `json:"paused"`
	Goal       string `json:"goal"`
	Remain     int    `json:"remain"` // seconds
	Total      int    `json:"total"`
	Breathing  bool   `json:"breathing"`
	ReportedAt int64  `json:"reported_at"` // unix ms
}

type device struct {
	conn net.Conn
	out  chan []byte
}

// Hub connects the store, the agent tracker, the Presto and the browsers.
type Hub struct {
	store   *Store
	tracker *Tracker
	fake    *FakeSource

	mu      sync.Mutex
	devices map[*device]struct{}
	subs    map[chan []byte]struct{}
	status  DeviceStatus
}

func NewHub(store *Store, tracker *Tracker, fake *FakeSource) *Hub {
	return &Hub{store: store, tracker: tracker, fake: fake, devices: map[*device]struct{}{},
		subs: map[chan []byte]struct{}{}, status: DeviceStatus{Phase: "idle"}}
}

// deviceLine is the full snapshot sent to the Presto.
func (h *Hub) deviceLine() []byte {
	snap := h.store.Snapshot()
	agents, note := h.tracker.Agents()
	now := time.Now()
	_, offset := now.Zone()
	b, _ := json.Marshal(map[string]any{
		"t": "state", "time": now.Unix(), "tz": offset, "pomos_today": snap.PomosToday,
		"goals": snap.Goals, "agents": agents, "agents_note": note, "settings": snap.Settings,
	})
	return b
}

// WebState is what the browser renders.
func (h *Hub) WebState() map[string]any {
	snap := h.store.Snapshot()
	agents, note := h.tracker.Agents()
	h.mu.Lock()
	st := h.status
	st.Connected = len(h.devices) > 0
	h.mu.Unlock()
	return map[string]any{
		"date": snap.Date, "goals": snap.Goals, "settings": snap.Settings, "pomos_today": snap.PomosToday,
		"focus_min": snap.FocusMin, "carry": snap.Carry, "agents": agents, "agents_note": note,
		"device": st, "fake_agents": h.fake != nil, "now": time.Now().UnixMilli(),
	}
}

// Changed pushes the current state to the Presto and to every browser.
func (h *Hub) Changed() {
	line := h.deviceLine()
	web, _ := json.Marshal(h.WebState())
	h.mu.Lock()
	defer h.mu.Unlock()
	for d := range h.devices {
		d.send(line)
	}
	for ch := range h.subs {
		select {
		case ch <- web:
		default: // a slow browser just misses an update; the next one is complete
		}
	}
}

func (h *Hub) notifyWeb() {
	web, _ := json.Marshal(h.WebState())
	h.mu.Lock()
	defer h.mu.Unlock()
	for ch := range h.subs {
		select {
		case ch <- web:
		default:
		}
	}
}

// Command sends a control message to the Presto. It reports whether one is connected.
func (h *Hub) Command(cmd map[string]any) bool {
	cmd["t"] = "cmd"
	b, _ := json.Marshal(cmd)
	h.mu.Lock()
	defer h.mu.Unlock()
	for d := range h.devices {
		d.send(b)
	}
	return len(h.devices) > 0
}

func (h *Hub) Subscribe() chan []byte {
	ch := make(chan []byte, 8)
	h.mu.Lock()
	h.subs[ch] = struct{}{}
	h.mu.Unlock()
	return ch
}

func (h *Hub) Unsubscribe(ch chan []byte) {
	h.mu.Lock()
	delete(h.subs, ch)
	h.mu.Unlock()
}

func (d *device) send(line []byte) {
	select {
	case d.out <- line:
	default:
		d.conn.Close() // hopelessly behind; it will reconnect and get a fresh snapshot
	}
}

// ServeDevices accepts Presto connections.
func (h *Hub) ServeDevices(ln net.Listener) {
	for {
		conn, err := ln.Accept()
		if err != nil {
			log.Printf("device listener: %v", err)
			return
		}
		go h.handleDevice(conn)
	}
}

func (h *Hub) handleDevice(conn net.Conn) {
	d := &device{conn: conn, out: make(chan []byte, 32)}
	log.Printf("presto connected from %s", conn.RemoteAddr())
	d.send(h.deviceLine())
	h.mu.Lock()
	h.devices[d] = struct{}{}
	h.mu.Unlock()
	h.notifyWeb()

	done := make(chan struct{})
	go func() { // writer: snapshots, commands and a ping that doubles as a clock sync
		ping := time.NewTicker(3 * time.Second)
		defer ping.Stop()
		for {
			var line []byte
			select {
			case <-done:
				return
			case line = <-d.out:
			case <-ping.C:
				now := time.Now()
				_, offset := now.Zone()
				line = []byte(fmt.Sprintf(`{"t":"ping","time":%d,"tz":%d}`, now.Unix(), offset))
			}
			conn.SetWriteDeadline(time.Now().Add(5 * time.Second))
			if _, err := conn.Write(append(line, '\n')); err != nil {
				conn.Close()
				return
			}
		}
	}()

	sc := bufio.NewScanner(conn)
	sc.Buffer(make([]byte, 4096), 64*1024)
	for {
		conn.SetReadDeadline(time.Now().Add(15 * time.Second))
		if !sc.Scan() {
			break
		}
		h.handleLine(sc.Bytes())
	}
	close(done)
	conn.Close()
	h.mu.Lock()
	delete(h.devices, d)
	h.mu.Unlock()
	log.Printf("presto disconnected (%s)", conn.RemoteAddr())
	h.notifyWeb()
}

func (h *Hub) handleLine(line []byte) {
	var msg struct {
		T         string `json:"t"`
		ID        string `json:"id"`
		Done      bool   `json:"done"`
		Goal      string `json:"goal"`
		Min       int    `json:"min"`
		Phase     string `json:"phase"`
		Paused    bool   `json:"paused"`
		Remain    int    `json:"remain"`
		Total     int    `json:"total"`
		Breathing bool   `json:"breathing"`
		FW        string `json:"fw"`
	}
	if err := json.Unmarshal(line, &msg); err != nil {
		log.Printf("bad line from presto: %q", line)
		return
	}
	switch msg.T {
	case "hello":
		log.Printf("presto firmware %s", msg.FW)
	case "status":
		h.mu.Lock()
		prev := h.status
		h.status = DeviceStatus{Phase: msg.Phase, Paused: msg.Paused, Goal: msg.Goal, Remain: msg.Remain,
			Total: msg.Total, Breathing: msg.Breathing, ReportedAt: time.Now().UnixMilli()}
		changed := prev.Phase != msg.Phase || prev.Paused != msg.Paused || prev.Goal != msg.Goal ||
			prev.Breathing != msg.Breathing || prev.Total != msg.Total || abs(prev.Remain-msg.Remain) > 5
		h.mu.Unlock()
		if changed {
			h.notifyWeb()
		}
	case "goal":
		if err := h.store.UpdateGoal(msg.ID, GoalPatch{Done: &msg.Done}); err != nil {
			log.Printf("goal update from presto: %v", err)
		}
		h.Changed()
	case "pomo":
		if err := h.store.AddSession("focus", msg.Goal, msg.Min); err != nil {
			log.Printf("saving session: %v", err)
		}
		log.Printf("focus session finished: %d min (goal %q)", msg.Min, msg.Goal)
		h.Changed()
	case "breathe":
		if err := h.store.AddSession("breathe", "", msg.Min); err != nil {
			log.Printf("saving session: %v", err)
		}
		h.notifyWeb()
	case "ack":
		log.Printf("agent alerts acknowledged on the presto")
	}
}

func abs(v int) int {
	if v < 0 {
		return -v
	}
	return v
}

// Beacon announces the device port so the Presto can find this machine
// without configuration. It broadcasts on every IPv4 network, plus
// loopback for the simulator.
func Beacon(beaconPort, devicePort int) {
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{})
	if err != nil {
		log.Printf("beacon disabled: %v", err)
		return
	}
	msg := []byte(fmt.Sprintf("PBUDDY1 %d", devicePort))
	for {
		targets := []net.IP{net.IPv4(127, 0, 0, 1), net.IPv4bcast}
		ifaces, _ := net.Interfaces()
		for _, ifc := range ifaces {
			if ifc.Flags&net.FlagUp == 0 || ifc.Flags&net.FlagBroadcast == 0 {
				continue
			}
			addrs, _ := ifc.Addrs()
			for _, a := range addrs {
				ipn, ok := a.(*net.IPNet)
				ip4 := ipn.IP.To4()
				if !ok || ip4 == nil || len(ipn.Mask) != 4 {
					continue
				}
				bc := make(net.IP, 4)
				for i := range bc {
					bc[i] = ip4[i] | ^ipn.Mask[i]
				}
				targets = append(targets, bc)
			}
		}
		for _, ip := range targets {
			conn.WriteToUDP(msg, &net.UDPAddr{IP: ip, Port: beaconPort})
		}
		time.Sleep(2 * time.Second)
	}
}

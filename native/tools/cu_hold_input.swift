// Bounded real macOS keyboard holds for an explicitly authorized CUA playtest.
// Usage: cu-hold-input <active-session.json> <keys joined by comma> <seconds>
// "mouse-left@X,Y" holds the left mouse button at X,Y: window points from the top-left of the
// app's window (screenshot pixels / the backing scale, 2 on Retina). The game's pointer is
// moved there first, so the Point to move mod steers toward it. Plain "mouse-left" uses the
// global cursor, which an app-directed CUA pointer does not move. Combine with keys, e.g.
// "mouse-left@830,300,space". Events go only to the app's process, never system-wide.
// "hid-mouse-left@X,Y" (lead-approved fallback, when process-posted mouse events don't reach the
// game): the same hold through the system event stream, which moves the real cursor. Only while
// the user is idle: HIDIdleTime above 120 s just before the hold, else it refuses. The cursor
// position is saved and restored. The hold aborts if a key is pressed or the cursor leaves the
// held point (someone at the machine); the tool's own events reset the HID idle clock, so that
// clock is checked only before.
import AppKit
import CoreGraphics
import Foundation
import IOKit

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data((message + "\n").utf8))
    exit(2)
}
let args = CommandLine.arguments
guard args.count == 4, let duration = Double(args[3]), duration > 0, duration <= 3 else {
    fail("Expected active-session.json, comma-separated keys, and duration 0..3 seconds")
}
let codes: [String: CGKeyCode] = ["w":13,"a":0,"s":1,"d":2,"space":49,"f":3,"q":12,"e":14,"c":8,"shift":56,"b":11]
// "mouse-left@X,Y" carries a comma: rejoin it after splitting the list.
var names: [String] = []
for part in args[2].split(separator: ",").map(String.init) {
    if let last = names.last, last.hasPrefix("mouse-left@") || last.hasPrefix("hid-mouse-left@"), !last.contains(","), Double(part) != nil {
        names[names.count - 1] = last + "," + part
    } else {
        names.append(part)
    }
}
guard !names.isEmpty, names.count <= 3, Set(names).count == names.count,
      names.allSatisfy({ codes[$0] != nil || $0.hasPrefix("mouse-left") || $0.hasPrefix("hid-mouse-left@") }) else { fail("Unsupported or repeated key") }
let recordURL = URL(fileURLWithPath: args[1]).standardizedFileURL
let data = try Data(contentsOf: recordURL)
guard let record = try JSONSerialization.jsonObject(with: data) as? [String: Any],
      let rawPid = record["pid"] as? Int, let appPath = record["app"] as? String,
      let app = NSRunningApplication(processIdentifier: pid_t(rawPid)),
      app.bundleURL?.standardizedFileURL.path == URL(fileURLWithPath: appPath).standardizedFileURL.path else {
    fail("The recorded playtest app must be alive with the expected bundle path")
}
let root = recordURL.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
// The lock exists only on the shared test machine (build/locked-app.sh); elsewhere there is none to check.
let sharedLocks = FileManager.default.fileExists(atPath: root.appendingPathComponent("build/locked-app.sh").path)
for name in [".petari-app.lock"] where sharedLocks {
    let owner = try String(contentsOf: root.appendingPathComponent("build/" + name + "/owner"), encoding: .utf8)
    guard owner.trimmingCharacters(in: .whitespacesAndNewlines) == "cu-playtest" else { fail("Playtest must own app lock") }
}
guard CGPreflightPostEventAccess() else { fail("macOS event-post permission is not available; no permission prompt was opened") }
let source = CGEventSource(stateID: .hidSystemState)
let eventLock = NSRecursiveLock()
var held: [CGKeyCode] = []
var mouseHeld = false
var mouseAt: CGPoint? = nil  // the held button's global position
var hidMouse = false          // the hold goes through the system event stream (hid-mouse-left)
var savedCursor: CGPoint? = nil
let holdStart = Date()
// Seconds since the last real input on this machine (IOHIDSystem's HIDIdleTime), nil if unknown.
func hidIdleSeconds() -> Double? {
    let service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOHIDSystem"))
    guard service != 0 else { return nil }
    defer { IOObjectRelease(service) }
    guard let value = IORegistryEntryCreateCFProperty(service, "HIDIdleTime" as CFString, kCFAllocatorDefault, 0)?.takeRetainedValue() as? NSNumber else { return nil }
    return value.doubleValue / 1_000_000_000
}
func cursor() -> CGPoint { return mouseAt ?? CGEvent(source: nil)?.location ?? .zero }
var windowNumber: Int64 = 0  // the app's window, stamped on process-posted mouse events
// The app's largest on-screen window, in global display points (top-left origin, as CGEvent).
func windowBounds() -> CGRect? {
    guard let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] else { return nil }
    var best: CGRect? = nil
    for info in list where (info[kCGWindowOwnerPID as String] as? Int) == rawPid && (info[kCGWindowLayer as String] as? Int) == 0 {
        guard let dict = info[kCGWindowBounds as String] as? NSDictionary, let rect = CGRect(dictionaryRepresentation: dict) else { continue }
        if best == nil || rect.width * rect.height > best!.width * best!.height {
            best = rect
            windowNumber = Int64(info[kCGWindowNumber as String] as? Int ?? 0)
        }
    }
    return best
}
func post(_ type: CGEventType, _ at: CGPoint) {
    let event = CGEvent(mouseEventSource: source, mouseType: type, mouseCursorPosition: at, mouseButton: .left)
    if hidMouse {
        event?.post(tap: .cghidEventTap)
    } else {
        // AppKit routes a mouse button event by the window under it; a process-posted event
        // carries none unless it is stamped.
        if windowNumber != 0 {
            event?.setIntegerValueField(.mouseEventWindowUnderMousePointer, value: windowNumber)
            event?.setIntegerValueField(.mouseEventWindowUnderMousePointerThatCanHandleThisEvent, value: windowNumber)
        }
        if type == .leftMouseDown || type == .leftMouseUp || type == .leftMouseDragged {
            event?.setIntegerValueField(.mouseEventClickState, value: 1)
        }
        event?.postToPid(pid_t(rawPid))
    }
}
func log(_ action: String, key: CGKeyCode? = nil, details: [String: Any] = [:]) {
    var item: [String: Any] = ["utc":ISO8601DateFormatter().string(from: Date()), "action":action,
                               "pid":rawPid,"keys":names,"seconds":duration,"mechanism":"CGEvent"]
    item.merge(details) { _, new in new }
    if let key = key { item["keycode"] = Int(key) }
    do {
        var bytes = try JSONSerialization.data(withJSONObject:item, options:[.sortedKeys]); bytes.append(10)
        let url = recordURL.deletingLastPathComponent().appendingPathComponent("actions.jsonl")
        let file = try FileHandle(forWritingTo:url); try file.seekToEnd(); try file.write(contentsOf:bytes); try file.close()
    } catch { FileHandle.standardError.write(Data(("input log error: \(error)\n").utf8)) }
}
func sameApp() -> Bool {
    guard let current = NSRunningApplication(processIdentifier: pid_t(rawPid)) else { return false }
    return current.bundleURL?.standardizedFileURL.path == URL(fileURLWithPath: appPath).standardizedFileURL.path
}
func frontDetails() -> [String: Any] {
    let front = NSWorkspace.shared.frontmostApplication
    return ["front_pid": Int(front?.processIdentifier ?? -1),
            "front_bundle_id": front?.bundleIdentifier ?? "nil",
            "front_bundle_path": front?.bundleURL?.path ?? "nil"]
}
func focused() -> Bool {
    guard sameApp() else {
        log("os-focus-target-invalid", details: frontDetails())
        return false
    }
    let first = frontDetails()
    if first["front_pid"] as? Int == rawPid { return true }
    log("os-focus-mismatch-first", details: first)
    RunLoop.current.run(until: Date().addingTimeInterval(0.04))
    let second = frontDetails()
    guard sameApp(), second["front_pid"] as? Int == rawPid else {
        log("os-focus-failure", details: second)
        FileHandle.standardError.write(Data(("Focus failure: first=\(first), second=\(second)\n").utf8))
        return false
    }
    log("os-focus-transient-recovered", details: second)
    return true
}
func release() {
    eventLock.lock(); defer { eventLock.unlock() }
    for key in held.reversed() {
        let front = frontDetails()
        let frontmost = sameApp() && front["front_pid"] as? Int == rawPid
        log(frontmost ? "os-key-up" : "os-key-up-focus-lost-cleanup", key:key, details: front)
        // Cleanup goes only to the original process even if another app gained focus.
        if sameApp() { CGEvent(keyboardEventSource:source, virtualKey:key, keyDown:false)?.postToPid(pid_t(rawPid)) }
    }
    held.removeAll()
    if mouseHeld {
        let at = cursor()
        log("os-mouse-up", details: ["x": Double(at.x), "y": Double(at.y)])
        if sameApp() {
            post(.leftMouseUp, at)
        }
        mouseHeld = false
    }
    if hidMouse, let saved = savedCursor {
        // Always, also when the hold failed before the button went down.
        CGWarpMouseCursorPosition(saved)
        log("os-hid-cursor-restored", details: ["x": Double(saved.x), "y": Double(saved.y)])
        savedCursor = nil
    }
}
let signalQueue = DispatchQueue(label:"cu-hold-input.signals")
var signalSources: [DispatchSourceSignal] = []
for number in [SIGTERM, SIGINT, SIGHUP] {
    signal(number, SIG_IGN)
    let handler = DispatchSource.makeSignalSource(signal:number, queue:signalQueue)
    handler.setEventHandler { release(); log("os-key-hold-signal-\(number)"); exit(128 + number) }
    handler.resume(); signalSources.append(handler)
}
guard focused() else { fail("The recorded playtest app must be frontmost") }
log("os-key-hold-start")
defer { release() }
for name in names {
    eventLock.lock()
    guard focused() else { eventLock.unlock(); release(); fail("Focus changed before keyDown") }
    if name.hasPrefix("hid-mouse-left@") {
        guard let idle = hidIdleSeconds(), idle > 120 else {
            eventLock.unlock(); release(); fail("hid-mouse-left needs the user idle: HIDIdleTime \(hidIdleSeconds() ?? -1) s, need > 120")
        }
        hidMouse = true
        savedCursor = CGEvent(source: nil)?.location
        log("os-hid-idle-ok", details: ["hid_idle_s": idle])
    }
    if name.hasPrefix("mouse-left") || name.hasPrefix("hid-mouse-left") {
        var at = CGEvent(source: nil)?.location ?? .zero
        var details: [String: Any] = [:]
        if let range = name.range(of: "@") {
            let xy = name[range.upperBound...].split(separator: ",").compactMap { Double($0) }
            guard xy.count == 2, let window = windowBounds() else {
                eventLock.unlock(); release(); fail("mouse-left@X,Y needs two numbers and the app's window on screen")
            }
            guard xy[0] >= 0, xy[1] >= 0, xy[0] <= Double(window.width), xy[1] <= Double(window.height) else {
                eventLock.unlock(); release(); fail("mouse-left@X,Y is outside the window (\(window.width) x \(window.height) points)")
            }
            at = CGPoint(x: window.origin.x + xy[0], y: window.origin.y + xy[1])
            details = ["window_x": Double(window.origin.x), "window_y": Double(window.origin.y), "window_w": Double(window.width),
                       "window_h": Double(window.height), "in_window_x": xy[0], "in_window_y": xy[1]]
            if hidMouse {
                // The real cursor: warp it synchronously, then wait until it is really there
                // before the button goes down and the activity guard starts.
                CGWarpMouseCursorPosition(at)
                CGAssociateMouseAndMouseCursorPosition(1)
                var reached = false
                let until = Date().addingTimeInterval(0.3)
                while Date() < until {
                    let now = CGEvent(source: nil)?.location ?? .zero
                    if abs(now.x - at.x) <= 2 && abs(now.y - at.y) <= 2 { reached = true; break }
                    Thread.sleep(forTimeInterval: 0.01)
                }
                guard reached else {
                    eventLock.unlock(); release(); fail("hid-mouse-left: the cursor did not reach the target in 300 ms; nothing pressed")
                }
            }
            post(.mouseMoved, at)  // the game's pointer goes there first
            // Let the game see the pointer arrive (a frame or two, even at a low frame rate)
            // before the button goes down.
            Thread.sleep(forTimeInterval: 0.3)
            if hidMouse {
                let now = CGEvent(source: nil)?.location ?? .zero
                guard abs(now.x - at.x) <= 2 && abs(now.y - at.y) <= 2 else {
                    eventLock.unlock(); release(); fail("hid-mouse-left: the cursor moved before the press (someone at the machine?); nothing pressed")
                }
            }
        }
        mouseAt = at
        mouseHeld = true
        details["x"] = Double(at.x); details["y"] = Double(at.y)
        log("os-mouse-down", details: details)
        post(.leftMouseDown, at)
        eventLock.unlock()
        continue
    }
    let key = codes[name]!
    held.append(key)
    log("os-key-down",key:key)
    CGEvent(keyboardEventSource:source, virtualKey:key, keyDown:true)?.postToPid(pid_t(rawPid))
    eventLock.unlock()
}
let deadline = Date().addingTimeInterval(duration)
while Date() < deadline {
    guard focused() else { release(); fail("Focus changed during hold") }
    if mouseHeld, let at = mouseAt { eventLock.lock(); post(.leftMouseDragged, at); eventLock.unlock() }  // still held, same spot
    if hidMouse, let at = mouseAt {
        // Someone at the machine: a key since the hold began, or the cursor moved off the held point.
        let keyIdle = CGEventSource.secondsSinceLastEventType(.hidSystemState, eventType: .keyDown)
        let now = CGEvent(source: nil)?.location ?? at
        if keyIdle < Date().timeIntervalSince(holdStart) || abs(now.x - at.x) > 2 || abs(now.y - at.y) > 2 {
            log("os-hid-user-activity-abort", details: ["key_idle_s": keyIdle, "x": Double(now.x), "y": Double(now.y)])
            release(); fail("User activity during the hid-mouse-left hold; released and restored the cursor")
        }
    }
    Thread.sleep(forTimeInterval:min(0.02, max(0,deadline.timeIntervalSinceNow)))
}
release()
log("os-key-hold-end")

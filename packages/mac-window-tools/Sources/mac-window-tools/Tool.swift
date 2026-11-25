import Foundation
import WindowTools

#if os(macOS)
import AppKit
import CoreGraphics
import ScreenCaptureKit

enum ToolError: Error, CustomStringConvertible {
    case screenPermission, windowMissing, output
    var description: String {
        switch self {
        case .screenPermission: return "Screen Recording access is not enabled for this program"
        case .windowMissing: return "no matching window appeared in time"
        case .output: return "could not write the PNG"
        }
    }
}

func readWindows() -> [WindowInfo] {
    let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: Any]] ?? []
    return list.map { entry in
        let bounds = entry[kCGWindowBounds as String] as? [String: Any] ?? [:]
        func coordinate(_ key: String) -> Double { (bounds[key] as? NSNumber)?.doubleValue ?? 0 }
        return WindowInfo(
            id: (entry[kCGWindowNumber as String] as? NSNumber)?.intValue ?? 0,
            pid: (entry[kCGWindowOwnerPID as String] as? NSNumber)?.intValue ?? 0,
            owner: entry[kCGWindowOwnerName as String] as? String ?? "",
            title: entry[kCGWindowName as String] as? String ?? "",
            layer: (entry[kCGWindowLayer as String] as? NSNumber)?.intValue ?? -1,
            onScreen: (entry[kCGWindowIsOnscreen as String] as? NSNumber)?.boolValue ?? false,
            x: coordinate("X"), y: coordinate("Y"), width: coordinate("Width"), height: coordinate("Height"))
    }
}

/// Takes a picture of one window by id. It reads that window's own pixels and never raises or activates it.
func capture(_ query: WindowQuery, to path: String, waitSeconds: Double) async throws {
    _ = NSApplication.shared
    _ = CGWindowListCopyWindowInfo(.optionAll, kCGNullWindowID)
    guard CGPreflightScreenCaptureAccess() else { throw ToolError.screenPermission }
    let clock = ContinuousClock()
    let deadline = clock.now.advanced(by: .seconds(waitSeconds))
    repeat {
        let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
        var windows: [Int: SCWindow] = [:]
        var infos: [WindowInfo] = []
        for window in content.windows {
            let info = WindowInfo(
                id: Int(window.windowID), pid: Int(window.owningApplication?.processID ?? 0),
                owner: window.owningApplication?.applicationName ?? "", title: window.title ?? "",
                layer: window.windowLayer, onScreen: window.isOnScreen,
                x: Double(window.frame.origin.x), y: Double(window.frame.origin.y),
                width: Double(window.frame.width), height: Double(window.frame.height))
            windows[info.id] = window
            infos.append(info)
        }
        if let best = largestWindow(in: infos.filter { query.matches($0) }), let window = windows[best.id] {
            let filter = SCContentFilter(desktopIndependentWindow: window)
            let image = try await SCScreenshotManager.captureImage(
                contentFilter: filter, configuration: SCStreamConfiguration())
            guard let destination = CGImageDestinationCreateWithURL(
                URL(fileURLWithPath: path) as CFURL, "public.png" as CFString, 1, nil)
            else { throw ToolError.output }
            CGImageDestinationAddImage(destination, image, nil)
            guard CGImageDestinationFinalize(destination) else { throw ToolError.output }
            print("\(best.id) \(Int(best.width))x\(Int(best.height)) \(best.title)")
            return
        }
        try await Task.sleep(for: .milliseconds(100))
    } while clock.now < deadline
    throw ToolError.windowMissing
}

@main
struct MacWindowTools {
    static func main() async {
        let command: Command
        do {
            command = try parseArguments(Array(CommandLine.arguments.dropFirst()))
        } catch let error as UsageError {
            FileHandle.standardError.write(Data("\(error.message)\n\(usageText)\n".utf8))
            exit(2)
        } catch {
            exit(2)
        }
        switch command {
        case .help:
            print(usageText)
        case .list(let query):
            for window in readWindows().filter({ query.matches($0) }) { print(listLine(window)) }
        case .find(let query):
            guard let best = largestWindow(in: readWindows().filter({ query.matches($0) })) else { exit(1) }
            print(findLine(best))
        case .capture(let query, let out, let wait):
            do {
                try await capture(query, to: out, waitSeconds: wait)
            } catch {
                FileHandle.standardError.write(Data("\(error)\n".utf8))
                exit(1)
            }
        }
    }
}
#else
@main
struct MacWindowTools {
    static func main() {
        FileHandle.standardError.write(Data("mac-window-tools runs on macOS only\n".utf8))
        exit(1)
    }
}
#endif

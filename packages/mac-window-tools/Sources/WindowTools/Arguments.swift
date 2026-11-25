import Foundation

public enum Command: Equatable {
    case list(WindowQuery)
    case find(WindowQuery)
    case capture(WindowQuery, out: String, waitSeconds: Double)
    case help
}

public struct UsageError: Error, Equatable {
    public let message: String
    public init(_ message: String) { self.message = message }
}

public let usageText = """
    usage:
      mac-window-tools list [--owner TEXT] [--pid N] [--layer N] [--on-screen]
          one line per window: id|pid|owner|layer|onscreen|x,y,w,h|title (the title is last; it may hold '|')
      mac-window-tools find (--pid N | --owner TEXT) [--min-size WxH] [--title-contains TEXT]
          the largest normal on-screen window: "<id> <title>"; exits 1 and prints nothing when there is none
      mac-window-tools capture --pid N --out FILE.png [--wait SECONDS] [--min-size WxH] [--title-contains TEXT]
          waits up to SECONDS (default 20) for that process's window, then writes it to a PNG; needs
          Screen Recording permission and macOS 14 or later
    """

private func parseSize(_ text: String) throws -> (Double, Double) {
    let parts = text.lowercased().split(separator: "x", omittingEmptySubsequences: false)
    guard parts.count == 2, let width = Double(parts[0]), let height = Double(parts[1]), width >= 0, height >= 0 else {
        throw UsageError("--min-size wants WIDTHxHEIGHT, for example 300x160")
    }
    return (width, height)
}

/// Reads the arguments after the program name.
public func parseArguments(_ arguments: [String]) throws -> Command {
    guard let name = arguments.first else { return .help }
    if name == "help" || name == "--help" || name == "-h" { return .help }
    guard ["list", "find", "capture"].contains(name) else { throw UsageError("unknown command \(name)") }

    var owner: String?
    var pid: Int?
    var layer: Int?
    var onScreen = false
    var minSize: (Double, Double)?
    var titleContains: String?
    var out: String?
    var wait = 20.0

    var index = 1
    func value(_ option: String) throws -> String {
        index += 1
        guard index < arguments.count else { throw UsageError("\(option) needs a value") }
        return arguments[index]
    }
    while index < arguments.count {
        let option = arguments[index]
        switch option {
        case "--owner": owner = try value(option)
        case "--pid":
            let text = try value(option)
            guard let number = Int(text), number > 0 else { throw UsageError("--pid wants a process id") }
            pid = number
        case "--layer":
            let text = try value(option)
            guard let number = Int(text) else { throw UsageError("--layer wants a whole number") }
            layer = number
        case "--on-screen": onScreen = true
        case "--min-size":
            let text = try value(option)
            minSize = try parseSize(text)
        case "--title-contains": titleContains = try value(option)
        case "--out": out = try value(option)
        case "--wait":
            let text = try value(option)
            guard let seconds = Double(text), seconds.isFinite, seconds >= 0 else {
                throw UsageError("--wait wants a number of seconds")
            }
            wait = seconds
        default: throw UsageError("unknown option \(option)")
        }
        index += 1
    }

    switch name {
    case "list":
        if out != nil { throw UsageError("list takes no --out") }
        return .list(WindowQuery(ownerContains: owner, pid: pid, layer: layer, onScreenOnly: onScreen, titleContains: titleContains))
    case "find", "capture":
        if owner == nil && pid == nil { throw UsageError("\(name) needs --pid or --owner") }
        if name == "capture" && pid == nil { throw UsageError("capture needs --pid, so it can only take the window of a process you started") }
        var query = WindowQuery.visibleAppWindow(pid: pid, ownerContains: owner)
        if let size = minSize {
            query.minWidth = size.0
            query.minHeight = size.1
        }
        query.titleContains = titleContains
        if name == "find" { return .find(query) }
        guard let file = out, !file.isEmpty else { throw UsageError("capture needs --out") }
        return .capture(query, out: file, waitSeconds: wait)
    default:
        throw UsageError("unknown command \(name)")
    }
}

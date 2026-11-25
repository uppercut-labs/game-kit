import Foundation

/// One window as the window server reports it.
public struct WindowInfo: Equatable {
    public var id: Int
    public var pid: Int
    public var owner: String
    public var title: String
    public var layer: Int
    public var onScreen: Bool
    public var x: Double
    public var y: Double
    public var width: Double
    public var height: Double

    public init(
        id: Int, pid: Int, owner: String, title: String, layer: Int, onScreen: Bool,
        x: Double, y: Double, width: Double, height: Double
    ) {
        self.id = id
        self.pid = pid
        self.owner = owner
        self.title = title
        self.layer = layer
        self.onScreen = onScreen
        self.x = x
        self.y = y
        self.width = width
        self.height = height
    }

    public var area: Double { width * height }
}

/// Which windows to keep. Every field that is set must match.
public struct WindowQuery: Equatable {
    /// Keep windows whose owner name contains this text.
    public var ownerContains: String?
    /// Keep windows of this process id.
    public var pid: Int?
    /// Keep windows on this layer. Layer 0 is a normal application window.
    public var layer: Int?
    public var onScreenOnly: Bool
    public var minWidth: Double
    public var minHeight: Double
    /// Keep windows whose title contains this text. It lets a caller tell an emulator's game window from a
    /// window of the same program that has the same owner but no game running, by a marker the title carries.
    public var titleContains: String?

    public init(
        ownerContains: String? = nil, pid: Int? = nil, layer: Int? = nil, onScreenOnly: Bool = false,
        minWidth: Double = 0, minHeight: Double = 0, titleContains: String? = nil
    ) {
        self.ownerContains = ownerContains
        self.pid = pid
        self.layer = layer
        self.onScreenOnly = onScreenOnly
        self.minWidth = minWidth
        self.minHeight = minHeight
        self.titleContains = titleContains
    }

    /// The query `find` and `capture` start from: a normal, visible window of at least 300 by 160 points.
    public static func visibleAppWindow(pid: Int? = nil, ownerContains: String? = nil) -> WindowQuery {
        WindowQuery(
            ownerContains: ownerContains, pid: pid, layer: 0, onScreenOnly: true, minWidth: 300, minHeight: 160)
    }

    public func matches(_ window: WindowInfo) -> Bool {
        if let owner = ownerContains, !owner.isEmpty, !window.owner.contains(owner) { return false }
        if let pid = pid, window.pid != pid { return false }
        if let layer = layer, window.layer != layer { return false }
        if onScreenOnly && !window.onScreen { return false }
        if window.width < minWidth || window.height < minHeight { return false }
        if let text = titleContains, !text.isEmpty, !window.title.contains(text) { return false }
        return true
    }
}

/// The largest window by area. A tie goes to the lower window id, so the answer does not depend on list order.
public func largestWindow(in windows: [WindowInfo]) -> WindowInfo? {
    windows.max { a, b in a.area != b.area ? a.area < b.area : a.id > b.id }
}

private func number(_ value: Double) -> String {
    if value.isFinite && abs(value) < 1e12 && value == value.rounded() { return String(Int(value)) }
    return String(value)
}

/// The line `list` prints: `id|pid|owner|layer|onscreen|x,y,w,h|title`. The title comes last because a title can
/// contain `|`; split on the first six bars and take the rest as the title.
public func listLine(_ window: WindowInfo) -> String {
    let bounds = [window.x, window.y, window.width, window.height].map(number).joined(separator: ",")
    return "\(window.id)|\(window.pid)|\(window.owner)|\(window.layer)|\(window.onScreen ? 1 : 0)|\(bounds)|\(window.title)"
}

/// The line `find` prints: the window id, then the title when there is one.
public func findLine(_ window: WindowInfo) -> String {
    window.title.isEmpty ? "\(window.id)" : "\(window.id) \(window.title)"
}

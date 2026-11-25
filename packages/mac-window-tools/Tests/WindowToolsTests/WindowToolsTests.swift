import XCTest
@testable import WindowTools

private func window(
    id: Int, pid: Int = 100, owner: String = "Emu", title: String = "", layer: Int = 0, onScreen: Bool = true,
    width: Double = 640, height: Double = 480
) -> WindowInfo {
    WindowInfo(id: id, pid: pid, owner: owner, title: title, layer: layer, onScreen: onScreen, x: 10, y: 20, width: width, height: height)
}

final class QueryTests: XCTestCase {
    func testEverySetFieldMustMatch() {
        let query = WindowQuery(ownerContains: "Emu", pid: 100, layer: 0, onScreenOnly: true, minWidth: 300, minHeight: 160, titleContains: "Game")
        XCTAssertTrue(query.matches(window(id: 1, title: "Game | 60 fps")))
        XCTAssertFalse(query.matches(window(id: 1, owner: "Other", title: "Game")))
        XCTAssertFalse(query.matches(window(id: 1, pid: 101, title: "Game")))
        XCTAssertFalse(query.matches(window(id: 1, title: "Game", layer: 25)))
        XCTAssertFalse(query.matches(window(id: 1, title: "Game", onScreen: false)))
        XCTAssertFalse(query.matches(window(id: 1, title: "Game", width: 299)))
        XCTAssertFalse(query.matches(window(id: 1, title: "Game", height: 159)))
        XCTAssertFalse(query.matches(window(id: 1, title: "Menu")))
    }

    func testAnEmptyQueryKeepsEverything() {
        XCTAssertTrue(WindowQuery().matches(window(id: 1, layer: -1, onScreen: false, width: 0, height: 0)))
        XCTAssertTrue(WindowQuery(ownerContains: "", titleContains: "").matches(window(id: 1)))
    }

    func testTheVisibleAppWindowQueryWantsANormalLargeVisibleWindow() {
        let query = WindowQuery.visibleAppWindow(pid: 7)
        XCTAssertEqual(query.layer, 0)
        XCTAssertTrue(query.onScreenOnly)
        XCTAssertEqual(query.minWidth, 300)
        XCTAssertEqual(query.minHeight, 160)
        XCTAssertEqual(query.pid, 7)
    }

    func testTheLargestWindowWinsAndATieGoesToTheLowerId() {
        let small = window(id: 5, width: 300, height: 200)
        let big = window(id: 9, width: 800, height: 600)
        let sameA = window(id: 4, width: 800, height: 600)
        XCTAssertEqual(largestWindow(in: [small, big])?.id, 9)
        XCTAssertEqual(largestWindow(in: [big, sameA, small])?.id, 4)
        XCTAssertEqual(largestWindow(in: [sameA, big, small])?.id, 4)
        XCTAssertNil(largestWindow(in: []))
    }
}

final class FormatTests: XCTestCase {
    func testAListLineKeepsTheTitleLastSoABarInItIsHarmless() {
        let line = listLine(window(id: 12, pid: 99, owner: "Dolphin", title: "Dolphin | JIT64 | 60 fps", layer: 0, onScreen: true, width: 1280.5, height: 720))
        XCTAssertEqual(line, "12|99|Dolphin|0|1|10,20,1280.5,720|Dolphin | JIT64 | 60 fps")
        let fields = line.split(separator: "|", maxSplits: 6, omittingEmptySubsequences: false)
        XCTAssertEqual(fields.count, 7)
        XCTAssertEqual(String(fields[6]), "Dolphin | JIT64 | 60 fps")
    }

    func testAFindLinePrintsTheIdAndTheTitleWhenThereIsOne() {
        XCTAssertEqual(findLine(window(id: 3, title: "Game")), "3 Game")
        XCTAssertEqual(findLine(window(id: 3)), "3")
    }
}

final class ArgumentTests: XCTestCase {
    func testListTakesFilters() throws {
        XCTAssertEqual(
            try parseArguments(["list", "--owner", "ares", "--on-screen", "--layer", "0"]),
            .list(WindowQuery(ownerContains: "ares", layer: 0, onScreenOnly: true)))
        XCTAssertEqual(try parseArguments(["list"]), .list(WindowQuery()))
    }

    func testFindStartsFromTheVisibleAppWindowQuery() throws {
        var expected = WindowQuery.visibleAppWindow(pid: 42)
        expected.minWidth = 100
        expected.minHeight = 50
        expected.titleContains = " | "
        XCTAssertEqual(try parseArguments(["find", "--pid", "42", "--min-size", "100x50", "--title-contains", " | "]), .find(expected))
        XCTAssertEqual(try parseArguments(["find", "--owner", "Flycast"]), .find(WindowQuery.visibleAppWindow(ownerContains: "Flycast")))
    }

    func testCaptureNeedsAPidAndAFile() throws {
        XCTAssertEqual(
            try parseArguments(["capture", "--pid", "42", "--out", "a.png", "--wait", "5"]),
            .capture(WindowQuery.visibleAppWindow(pid: 42), out: "a.png", waitSeconds: 5))
        XCTAssertEqual(
            try parseArguments(["capture", "--pid", "42", "--out", "a.png"]),
            .capture(WindowQuery.visibleAppWindow(pid: 42), out: "a.png", waitSeconds: 20))
        XCTAssertThrowsError(try parseArguments(["capture", "--owner", "ares", "--out", "a.png"]))
        XCTAssertThrowsError(try parseArguments(["capture", "--pid", "42"]))
    }

    func testBadInputIsAUsageError() {
        XCTAssertThrowsError(try parseArguments(["frobnicate"]))
        XCTAssertThrowsError(try parseArguments(["list", "--pid", "abc"]))
        XCTAssertThrowsError(try parseArguments(["list", "--pid"]))
        XCTAssertThrowsError(try parseArguments(["find"]))
        XCTAssertThrowsError(try parseArguments(["find", "--pid", "1", "--min-size", "wide"]))
        XCTAssertThrowsError(try parseArguments(["list", "--out", "x"]))
        XCTAssertThrowsError(try parseArguments(["list", "--wait", "-1"]))
    }

    func testNoArgumentsIsHelp() throws {
        XCTAssertEqual(try parseArguments([]), .help)
        XCTAssertEqual(try parseArguments(["--help"]), .help)
    }
}

import Foundation
import CoreLocation

// MARK: - Grid Locator

/// Converts between GPS coordinates and Maidenhead grid squares.
public enum GridLocator {

    // MARK: - Coordinate → Grid

    /// Returns a 6-character Maidenhead locator for a GPS coordinate.
    public static func grid(from coord: CLLocationCoordinate2D) -> String {
        grid(lat: coord.latitude, lon: coord.longitude)
    }

    /// Returns a 4 or 6-character Maidenhead locator from decimal degrees.
    /// - Parameters:
    ///   - lat: Latitude  −90 … +90
    ///   - lon: Longitude −180 … +180
    ///   - precision: 4 (field+square) or 6 (field+square+subsquare)
    public static func grid(lat: Double, lon: Double, precision: Int = 4) -> String {
        var lat = lat + 90    // 0 … 180
        var lon = lon + 180   // 0 … 360

        // Field (A–R)
        let lonField = Int(lon / 20)
        let latField = Int(lat / 10)
        lon -= Double(lonField) * 20
        lat -= Double(latField) * 10

        // Square (0–9)
        let lonSquare = Int(lon / 2)
        let latSquare = Int(lat / 1)
        lon -= Double(lonSquare) * 2
        lat -= Double(latSquare) * 1

        let fieldChars = "ABCDEFGHIJKLMNOPQR"
        let fc0 = fieldChars[fieldChars.index(fieldChars.startIndex, offsetBy: lonField)]
        let fc1 = fieldChars[fieldChars.index(fieldChars.startIndex, offsetBy: latField)]
        let sq0 = Character(String(lonSquare))
        let sq1 = Character(String(latSquare))

        if precision <= 4 {
            return "\(fc0)\(fc1)\(sq0)\(sq1)"
        }

        // Sub-square (A–X)
        let lonSub = Int(lon / 2 * 24)
        let latSub = Int(lat * 24)
        let subChars = "ABCDEFGHIJKLMNOPQRSTUVWX"
        let sub0 = subChars[subChars.index(subChars.startIndex, offsetBy: min(lonSub, 23))]
        let sub1 = subChars[subChars.index(subChars.startIndex, offsetBy: min(latSub, 23))]

        return "\(fc0)\(fc1)\(sq0)\(sq1)\(sub0)\(sub1)"
    }

    // MARK: - Grid → Coordinate

    /// Returns the centre coordinate of a Maidenhead grid square.
    public static func coordinate(from grid: String) -> CLLocationCoordinate2D? {
        let g = grid.uppercased()
        guard g.count >= 4 else { return nil }
        let chars = Array(g)

        guard chars[0].isLetter, chars[1].isLetter,
              chars[2].isNumber, chars[3].isNumber else { return nil }

        let lonField = Double(chars[0].asciiValue! - UInt8(UnicodeScalar("A").value))
        let latField = Double(chars[1].asciiValue! - UInt8(UnicodeScalar("A").value))
        let lonSq    = Double(chars[2].asciiValue! - UInt8(UnicodeScalar("0").value))
        let latSq    = Double(chars[3].asciiValue! - UInt8(UnicodeScalar("0").value))

        var lon = lonField * 20 + lonSq * 2 - 180
        var lat = latField * 10 + latSq      -  90

        if g.count >= 6, chars[4].isLetter, chars[5].isLetter {
            let lonSub = Double(chars[4].asciiValue! - UInt8(UnicodeScalar("A").value))
            let latSub = Double(chars[5].asciiValue! - UInt8(UnicodeScalar("A").value))
            lon += lonSub * 2.0 / 24.0 + 1.0 / 24.0
            lat += latSub * 1.0 / 24.0 + 0.5 / 24.0
        } else {
            lon += 1.0   // centre of 2° field
            lat += 0.5   // centre of 1° field
        }

        return CLLocationCoordinate2D(latitude: lat, longitude: lon)
    }

    // MARK: - Distance & Bearing

    /// Distance in kilometres between two grid squares.
    public static func distance(from: String, to: String) -> Double? {
        guard let c1 = coordinate(from: from),
              let c2 = coordinate(from: to) else { return nil }
        let loc1 = CLLocation(latitude: c1.latitude, longitude: c1.longitude)
        let loc2 = CLLocation(latitude: c2.latitude, longitude: c2.longitude)
        return loc1.distance(from: loc2) / 1000.0
    }

    /// True bearing (degrees) from one grid to another.
    public static func bearing(from: String, to: String) -> Double? {
        guard let c1 = coordinate(from: from),
              let c2 = coordinate(from: to) else { return nil }

        let lat1 = c1.latitude  * .pi / 180
        let lat2 = c2.latitude  * .pi / 180
        let dLon = (c2.longitude - c1.longitude) * .pi / 180

        let y = sin(dLon) * cos(lat2)
        let x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon)
        let θ = atan2(y, x) * 180 / .pi
        return (θ + 360).truncatingRemainder(dividingBy: 360)
    }

    // MARK: - Validation

    /// Returns true if the string is a syntactically valid Maidenhead locator (4 or 6 chars).
    public static func isValid(_ grid: String) -> Bool {
        let g = grid.uppercased()
        guard g.count == 4 || g.count == 6 else { return false }
        let ch = Array(g)
        guard ch[0].isLetter && ch[1].isLetter && ch[2].isNumber && ch[3].isNumber else { return false }
        if g.count == 6 { guard ch[4].isLetter && ch[5].isLetter else { return false } }
        let field0 = Int(ch[0].asciiValue! - UInt8(UnicodeScalar("A").value))
        let field1 = Int(ch[1].asciiValue! - UInt8(UnicodeScalar("A").value))
        return field0 < 18 && field1 < 18
    }
}

// MARK: - TimeSync

/// Monitors the UTC clock and fires a callback at the start of each T/R period.
public final class TimeSync {

    public var onPeriodStart: ((Date) -> Void)?

    private var timer: Timer?
    private let mode: RadioMode

    public init(mode: RadioMode) {
        self.mode = mode
    }

    public func start() {
        timer?.invalidate()
        scheduleNextTick()
    }

    public func stop() {
        timer?.invalidate()
        timer = nil
    }

    private func scheduleNextTick() {
        let cycle = mode.cycleLength
        let now   = Date().timeIntervalSince1970
        let next  = (floor(now / cycle) + 1) * cycle
        let delay = next - now

        timer = Timer.scheduledTimer(withTimeInterval: delay, repeats: false) { [weak self] _ in
            self?.onPeriodStart?(Date(timeIntervalSince1970: next))
            self?.scheduleNextTick()
        }
    }

    /// Returns seconds elapsed since the last period boundary, in 0 ..< cycleLength.
    public func secondsIntoPeriod() -> Double {
        let now   = Date().timeIntervalSince1970
        let cycle = mode.cycleLength
        return now.truncatingRemainder(dividingBy: cycle)
    }

    /// Returns seconds until the next period boundary.
    public func secondsToNextPeriod() -> Double {
        mode.cycleLength - secondsIntoPeriod()
    }
}

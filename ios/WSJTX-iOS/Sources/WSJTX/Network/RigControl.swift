import Foundation
import Network

// MARK: - Rig Control

/// Sends frequency and mode commands to a rig controller via UDP, using both
/// the Hamlib `rigctld` protocol and the WSJT-X UDP broadcast protocol so that
/// external logging/cluster programs receive updates.
public final class RigControl: ObservableObject {

    @Published public private(set) var isConnected = false
    @Published public private(set) var currentFrequency: Double = 14_074_000
    @Published public private(set) var lastError: String?

    private var hamlibConnection: NWConnection?
    private var wsjtxListener: NWListener?
    private let settings: AppSettings
    private let queue = DispatchQueue(label: "wsjtx.rigcontrol", qos: .userInitiated)

    public init(settings: AppSettings) {
        self.settings = settings
    }

    // MARK: - Connection

    public func connect() {
        let host = NWEndpoint.Host(settings.rigHost)
        let port = NWEndpoint.Port(rawValue: UInt16(settings.rigPort)) ?? .init(integerLiteral: 4532)
        let params = NWParameters.tcp
        params.allowLocalEndpointReuse = true

        hamlibConnection = NWConnection(host: host, port: port, using: params)
        hamlibConnection?.stateUpdateHandler = { [weak self] state in
            DispatchQueue.main.async {
                switch state {
                case .ready:       self?.isConnected = true; self?.lastError = nil
                case .failed(let e): self?.isConnected = false; self?.lastError = e.localizedDescription
                default:           break
                }
            }
        }
        hamlibConnection?.start(queue: queue)
        startWSJTXListener()
    }

    public func disconnect() {
        hamlibConnection?.cancel()
        wsjtxListener?.cancel()
        isConnected = false
    }

    // MARK: - Frequency / Mode

    /// Set VFO-A frequency (Hz) and mode.
    public func setFrequency(_ hz: Double, mode: RadioMode) {
        let freqCmd = "F \(Int(hz))\n"
        send(string: freqCmd)

        let modeStr = hamlibModeString(mode)
        let modeCmd = "M \(modeStr) 0\n"
        send(string: modeCmd)

        DispatchQueue.main.async { self.currentFrequency = hz }
        broadcastWSJTXFrequency(hz: hz, mode: mode)
    }

    public func tune() {
        send(string: "T 1\n")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
            self.send(string: "T 0\n")
        }
    }

    // MARK: - Hamlib helpers

    private func hamlibModeString(_ mode: RadioMode) -> String {
        switch mode {
        case .ft8, .ft4, .jt65, .jt9, .wspr, .q65: return "USB"
        }
    }

    private func send(string: String) {
        guard let conn = hamlibConnection else { return }
        let data = Data(string.utf8)
        conn.send(content: data, completion: .idempotent)
    }

    // MARK: - WSJT-X UDP broadcast

    /// Broadcasts a WSJT-X compatible "Status" datagram on the local network
    /// so that JTAlert, GridTracker, and PSK Reporter pick up our frequency.
    private func broadcastWSJTXFrequency(hz: Double, mode: RadioMode) {
        let payload = wsjtxStatusPayload(hz: hz, mode: mode)
        let host = NWEndpoint.Host("255.255.255.255")
        let port = NWEndpoint.Port(rawValue: UInt16(settings.wsjtxPort)) ?? .init(integerLiteral: 2237)
        let conn = NWConnection(host: host, port: port, using: .udp)
        conn.start(queue: queue)
        conn.send(content: payload, completion: .contentProcessed { _ in conn.cancel() })
    }

    private func wsjtxStatusPayload(hz: Double, mode: RadioMode) -> Data {
        // WSJT-X UDP protocol magic: 0xADBCCBDA, schema 2, type 1 (Status)
        var d = Data()
        d.appendUInt32(0xADBCCBDA)  // magic
        d.appendUInt32(2)           // schema version
        d.appendUInt32(1)           // message type: Status
        d.appendUInt32(0)           // ID length placeholder
        d.appendWSJTXString("WSJTX-iOS")
        d.appendUInt64(UInt64(hz))
        d.appendWSJTXString(mode.rawValue)
        d.appendWSJTXString("")     // DX call
        d.appendWSJTXString("")     // report
        d.appendWSJTXString("")     // TX mode
        d.appendBool(false)         // TX enabled
        d.appendBool(false)         // transmitting
        d.appendBool(false)         // decoding
        d.appendUInt32(0)           // RX DF
        d.appendUInt32(0)           // TX DF
        d.appendWSJTXString("")     // DE call
        d.appendWSJTXString("")     // DE grid
        d.appendWSJTXString("")     // DX grid
        d.appendBool(false)         // TX watchdog
        d.appendWSJTXString("")     // sub-mode
        d.appendBool(false)         // fast mode
        d.appendUInt8(0)            // special op mode
        return d
    }

    // MARK: - Incoming WSJT-X listener

    private func startWSJTXListener() {
        let port = NWEndpoint.Port(rawValue: UInt16(settings.wsjtxPort)) ?? .init(integerLiteral: 2237)
        do {
            wsjtxListener = try NWListener(using: .udp, on: port)
            wsjtxListener?.newConnectionHandler = { [weak self] conn in
                self?.handleIncomingUDP(conn)
            }
            wsjtxListener?.start(queue: queue)
        } catch {
            print("[RigControl] UDP listener error: \(error)")
        }
    }

    private func handleIncomingUDP(_ conn: NWConnection) {
        conn.start(queue: queue)
        conn.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, _, _ in
            guard let data else { return }
            self?.parseWSJTXDatagram(data)
        }
    }

    private func parseWSJTXDatagram(_ data: Data) {
        guard data.count >= 8 else { return }
        var offset = 0
        let magic = data.readUInt32(&offset)
        guard magic == 0xADBCCBDA else { return }
        _ = data.readUInt32(&offset)   // schema
        let msgType = data.readUInt32(&offset)

        if msgType == 1 {  // Status
            _ = data.readUInt32(&offset)  // ID length
            _ = data.readWSJTXString(&offset)  // ID
            let freq = data.readUInt64(&offset)
            DispatchQueue.main.async { self.currentFrequency = Double(freq) }
        }
    }
}

// MARK: - PSK Reporter

/// Uploads decoded signal reports to pskreporter.info.
public final class PSKReporter {

    private let myCall: String
    private let myGrid: String
    private var pending: [(call: String, freq: Double, mode: RadioMode, snr: Int, time: Date)] = []
    private let uploadInterval: TimeInterval = 300   // 5 minutes

    public init(myCall: String, myGrid: String) {
        self.myCall = myCall
        self.myGrid = myGrid
    }

    public func addSpot(call: String, freq: Double, mode: RadioMode, snr: Int) {
        pending.append((call: call, freq: freq, mode: mode, snr: snr, time: Date()))
    }

    public func upload() {
        guard !pending.isEmpty, !myCall.isEmpty else { return }
        let body = buildXML()
        pending.removeAll()

        guard let url = URL(string: "https://www.pskreporter.info/report") else { return }
        var req = URLRequest(url: url)
        req.httpMethod = "POST"
        req.setValue("application/xml", forHTTPHeaderField: "Content-Type")
        req.httpBody = Data(body.utf8)
        URLSession.shared.dataTask(with: req).resume()
    }

    private func buildXML() -> String {
        let fmt = ISO8601DateFormatter()
        var xml = """
        <?xml version="1.0" encoding="UTF-8"?>
        <pskreport>
          <receiverInfo callsign="\(myCall)" locator="\(myGrid)" programid="WSJTX-iOS"/>
          <activeReceiver callsign="\(myCall)" locator="\(myGrid)"/>

        """
        for s in pending {
            xml += """
              <receptionReport callsign="\(s.call)" receiverCallsign="\(myCall)"
                frequency="\(Int(s.freq))" mode="\(s.mode.rawValue)"
                sNR="\(s.snr)" flowStartSeconds="\(Int(s.time.timeIntervalSince1970))"/>
            """
        }
        xml += "</pskreport>"
        return xml
    }
}

// MARK: - Data encoding helpers

private extension Data {
    mutating func appendUInt8(_ v: UInt8)  { append(v) }
    mutating func appendBool(_ v: Bool)    { appendUInt8(v ? 1 : 0) }

    mutating func appendUInt32(_ v: UInt32) {
        var n = v.bigEndian
        Swift.withUnsafeBytes(of: &n) { append(contentsOf: $0) }
    }
    mutating func appendUInt64(_ v: UInt64) {
        var n = v.bigEndian
        Swift.withUnsafeBytes(of: &n) { append(contentsOf: $0) }
    }
    mutating func appendWSJTXString(_ s: String) {
        if s.isEmpty {
            appendUInt32(0xFFFFFFFF)
            return
        }
        appendUInt32(UInt32(s.utf8.count))
        append(contentsOf: s.utf8)
    }

    func readUInt32(_ offset: inout Int) -> UInt32 {
        guard offset + 4 <= count else { return 0 }
        let v = subdata(in: offset ..< offset + 4).withUnsafeBytes {
            $0.loadUnaligned(as: UInt32.self).bigEndian
        }
        offset += 4
        return v
    }
    func readUInt64(_ offset: inout Int) -> UInt64 {
        guard offset + 8 <= count else { return 0 }
        let v = subdata(in: offset ..< offset + 8).withUnsafeBytes {
            $0.loadUnaligned(as: UInt64.self).bigEndian
        }
        offset += 8
        return v
    }
    func readWSJTXString(_ offset: inout Int) -> String {
        let len = readUInt32(&offset)
        if len == 0xFFFFFFFF { return "" }
        guard offset + Int(len) <= count else { return "" }
        let s = String(data: subdata(in: offset ..< offset + Int(len)), encoding: .utf8) ?? ""
        offset += Int(len)
        return s
    }
}

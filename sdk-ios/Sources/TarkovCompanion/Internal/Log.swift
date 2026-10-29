import os

/// The host's logger, or the unified logging system when it gave none.
struct Log: Sendable {
    private static let systemLog = OSLog(subsystem: "com.tarkovcompanion.sdk", category: "TarkovCompanion")

    private let hostLogger: (@Sendable (LogLevel, String) -> Void)?

    init(hostLogger: (@Sendable (LogLevel, String) -> Void)?) {
        self.hostLogger = hostLogger
    }

    func log(_ level: LogLevel, _ message: String) {
        if let hostLogger {
            hostLogger(level, message)
            return
        }

        os_log("%{public}@", log: Self.systemLog, type: Self.systemLogType(level), message)
    }

    func debug(_ message: String) {
        log(.debug, message)
    }

    func warning(_ message: String) {
        log(.warning, message)
    }

    func error(_ message: String) {
        log(.error, message)
    }

    private static func systemLogType(_ level: LogLevel) -> OSLogType {
        switch level {
        case .debug:
            return .debug
        case .info:
            return .info
        case .warning:
            return .default
        case .error:
            return .error
        }
    }
}

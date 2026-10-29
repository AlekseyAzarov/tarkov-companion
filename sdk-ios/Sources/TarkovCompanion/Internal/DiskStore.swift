import CryptoKit
import Foundation

protocol KeyValueStore: Sendable {
    func value(forKey key: String) throws -> Data?

    func store(_ value: Data, forKey key: String) throws

    func deleteValue(forKey key: String) throws
}

/// Opaque values by key; the core stamps its own entries. Writes are atomic
/// (a temporary file and a rename), so a killed process leaves the old or the
/// new entry, never half of one.
final class DiskStore: KeyValueStore, @unchecked Sendable {
    private static let entryFileExtension = "bin"

    // 128 bits of SHA-256: collision-free for any realistic number of entries.
    private static let fileNameBytes = 16

    private let directory: URL
    private let maximumEntryCountOrNilForUnlimited: Int?
    private let lock = NSLock()

    init(directory: URL, maximumEntryCountOrNilForUnlimited: Int?) {
        self.directory = directory
        self.maximumEntryCountOrNilForUnlimited = maximumEntryCountOrNilForUnlimited
    }

    func value(forKey key: String) throws -> Data? {
        lock.lock()
        defer { lock.unlock() }

        let file = fileURL(forKey: key)
        if !FileManager.default.fileExists(atPath: file.path) {
            return nil
        }

        do {
            return try Data(contentsOf: file)
        } catch {
            // Unreadable entry (e.g. a disk error): drop it.
            try? FileManager.default.removeItem(at: file)
            return nil
        }
    }

    func store(_ value: Data, forKey key: String) throws {
        lock.lock()
        defer { lock.unlock() }

        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try value.write(to: fileURL(forKey: key), options: .atomic)
        deleteOldestEntriesOverLimit()
    }

    func deleteValue(forKey key: String) throws {
        lock.lock()
        defer { lock.unlock() }

        let file = fileURL(forKey: key)
        if FileManager.default.fileExists(atPath: file.path) {
            try FileManager.default.removeItem(at: file)
        }
    }

    private func deleteOldestEntriesOverLimit() {
        guard let maximumEntryCount = maximumEntryCountOrNilForUnlimited else {
            return
        }

        let entries = (try? FileManager.default.contentsOfDirectory(
            at: directory,
            includingPropertiesForKeys: [.contentModificationDateKey]
        )) ?? []
        let entryFiles = entries.filter { file in file.pathExtension == Self.entryFileExtension }
        if entryFiles.count <= maximumEntryCount {
            return
        }

        let oldestFirst = entryFiles.sorted { first, second in
            modificationDate(of: first) < modificationDate(of: second)
        }
        for file in oldestFirst.prefix(entryFiles.count - maximumEntryCount) {
            try? FileManager.default.removeItem(at: file)
        }
    }

    private func modificationDate(of file: URL) -> Date {
        let values = try? file.resourceValues(forKeys: [.contentModificationDateKey])

        return values?.contentModificationDate ?? .distantPast
    }

    // Keys are hashed: file names stay short and don't leak search queries.
    private func fileURL(forKey key: String) -> URL {
        let digest = SHA256.hash(data: Data(key.utf8))
        let name = digest.prefix(Self.fileNameBytes).map { byte in String(format: "%02x", byte) }.joined()

        return directory.appendingPathComponent(name).appendingPathExtension(Self.entryFileExtension)
    }
}

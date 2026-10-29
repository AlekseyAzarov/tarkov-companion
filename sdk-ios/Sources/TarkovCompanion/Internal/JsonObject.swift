import Foundation

struct JsonReadError: Error, CustomStringConvertible {
    let description: String
}

/// Typed reads from a JSON object, failing with the name of the field that is
/// missing or of the wrong type, as org.json's getters do on Android.
struct JsonObject {
    private let fields: [String: Any]

    init(text: String) throws {
        try self.init(data: Data(text.utf8))
    }

    init(data: Data) throws {
        guard let fields = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw JsonReadError(description: "not a JSON object")
        }

        self.fields = fields
    }

    private init(fields: [String: Any]) {
        self.fields = fields
    }

    func string(_ name: String) throws -> String {
        guard let value = fields[name] as? String else {
            throw JsonReadError(description: "\(name) is not a string")
        }

        return value
    }

    func optionalString(_ name: String) -> String? {
        fields[name] as? String
    }

    func integer(_ name: String) throws -> Int64 {
        guard let value = optionalInteger(name) else {
            throw JsonReadError(description: "\(name) is not an integer")
        }

        return value
    }

    func optionalInteger(_ name: String) -> Int64? {
        guard let number = fields[name] as? NSNumber, !isBoolean(number) else {
            return nil
        }

        return number.int64Value
    }

    func unsignedInteger(_ name: String) throws -> UInt64 {
        guard let number = fields[name] as? NSNumber, !isBoolean(number), number.int64Value >= 0 else {
            throw JsonReadError(description: "\(name) is not an unsigned integer")
        }

        return number.uint64Value
    }

    func boolean(_ name: String) throws -> Bool {
        guard let number = fields[name] as? NSNumber, isBoolean(number) else {
            throw JsonReadError(description: "\(name) is not a boolean")
        }

        return number.boolValue
    }

    func optionalBoolean(_ name: String) -> Bool? {
        guard let number = fields[name] as? NSNumber, isBoolean(number) else {
            return nil
        }

        return number.boolValue
    }

    func object(_ name: String) throws -> JsonObject {
        guard let object = try optionalObject(name) else {
            throw JsonReadError(description: "\(name) is not an object")
        }

        return object
    }

    /// Nil when the field is missing or null.
    func optionalObject(_ name: String) throws -> JsonObject? {
        switch fields[name] {
        case nil, is NSNull:
            return nil
        case let object as [String: Any]:
            return JsonObject(fields: object)
        default:
            throw JsonReadError(description: "\(name) is not an object")
        }
    }

    func objects(_ name: String) throws -> [JsonObject] {
        guard let array = fields[name] as? [Any] else {
            throw JsonReadError(description: "\(name) is not an array")
        }

        return try array.map { element in
            guard let object = element as? [String: Any] else {
                throw JsonReadError(description: "\(name) holds something other than objects")
            }

            return JsonObject(fields: object)
        }
    }

    func stringMap(_ name: String) throws -> [String: String] {
        guard let object = fields[name] as? [String: Any] else {
            throw JsonReadError(description: "\(name) is not an object")
        }

        return try object.mapValues { value in
            guard let text = value as? String else {
                throw JsonReadError(description: "\(name) holds something other than strings")
            }

            return text
        }
    }

    // JSONSerialization reads true and false as NSNumber too.
    private func isBoolean(_ number: NSNumber) -> Bool {
        CFGetTypeID(number) == CFBooleanGetTypeID()
    }
}

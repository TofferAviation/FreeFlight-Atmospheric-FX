#include "diagnostics/ReplayFile.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace ffatmo::diagnostics {
namespace {

constexpr std::array<char, 8> kMagic {{'F', 'F', 'A', 'T', 'R', 'P', 'L', '1'}};
constexpr std::uint32_t kEndianMarker = 0x01020304u;
constexpr std::uint32_t kChunkMetadata = 1u;
constexpr std::uint32_t kChunkSnapshotBlock = 2u;
constexpr std::uint32_t kChunkEnd = 0xFFFFFFFFu;
constexpr std::size_t kQueueCapacity = 2048;
constexpr std::size_t kSnapshotBlockCount = 128;
constexpr std::uint64_t kMaximumChunkBytes = 256ull * 1024ull * 1024ull;

class ByteWriter {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void u16(std::uint16_t value) {
        for (int shift = 0; shift < 16; shift += 8) u8(static_cast<std::uint8_t>(value >> shift));
    }
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) u8(static_cast<std::uint8_t>(value >> shift));
    }
    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) u8(static_cast<std::uint8_t>(value >> shift));
    }
    void f32(float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }
    void f64(double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u64(bits);
    }
    void raw(const char* data, std::size_t size) {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(data);
        bytes_.insert(bytes_.end(), begin, begin + size);
    }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

class ByteReader {
public:
    explicit ByteReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    bool u8(std::uint8_t& value) {
        if (offset_ >= bytes_.size()) return false;
        value = bytes_[offset_++];
        return true;
    }
    bool u16(std::uint16_t& value) {
        value = 0;
        for (int shift = 0; shift < 16; shift += 8) {
            std::uint8_t byte = 0;
            if (!u8(byte)) return false;
            value |= static_cast<std::uint16_t>(byte) << shift;
        }
        return true;
    }
    bool u32(std::uint32_t& value) {
        value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            std::uint8_t byte = 0;
            if (!u8(byte)) return false;
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }
    bool u64(std::uint64_t& value) {
        value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            std::uint8_t byte = 0;
            if (!u8(byte)) return false;
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
    }
    bool f32(float& value) {
        std::uint32_t bits = 0;
        if (!u32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }
    bool f64(double& value) {
        std::uint64_t bits = 0;
        if (!u64(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }
    bool raw(char* destination, std::size_t size) {
        if (size > bytes_.size() - offset_) return false;
        std::memcpy(destination, bytes_.data() + offset_, size);
        offset_ += size;
        return true;
    }
    bool finished() const { return offset_ == bytes_.size(); }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
};

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t value = 0xFFFFFFFFu;
    for (std::size_t index = 0; index < size; ++index) {
        value ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (value & 1u);
            value = (value >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~value;
}

void writeBytes(std::ofstream& stream, const std::vector<std::uint8_t>& bytes) {
    if (!bytes.empty()) {
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
}

bool readExact(std::ifstream& stream, std::vector<std::uint8_t>& bytes, std::size_t size) {
    bytes.resize(size);
    if (size == 0) return true;
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    return stream.gcount() == static_cast<std::streamsize>(size);
}

void appendVec3f(ByteWriter& writer, const engine::Vec3f& value) {
    writer.f32(value.x); writer.f32(value.y); writer.f32(value.z);
}
void appendVec3d(ByteWriter& writer, const engine::Vec3d& value) {
    writer.f64(value.x); writer.f64(value.y); writer.f64(value.z);
}
bool readVec3f(ByteReader& reader, engine::Vec3f& value) {
    return reader.f32(value.x) && reader.f32(value.y) && reader.f32(value.z);
}
bool readVec3d(ByteReader& reader, engine::Vec3d& value) {
    return reader.f64(value.x) && reader.f64(value.y) && reader.f64(value.z);
}

template <std::size_t N>
void appendFloatArray(ByteWriter& writer, const std::array<float, N>& values) {
    for (float value : values) writer.f32(value);
}
template <std::size_t N>
bool readFloatArray(ByteReader& reader, std::array<float, N>& values) {
    for (float& value : values) if (!reader.f32(value)) return false;
    return true;
}

void appendSnapshot(ByteWriter& writer, const engine::SimulatorSnapshot& snapshot) {
    writer.u32(snapshot.schemaVersion);
    writer.u32(snapshot.lifecycleFlags);
    writer.u64(snapshot.sequenceNumber);
    writer.u64(snapshot.validityMask);
    writer.f64(snapshot.simulatorUptimeSeconds);
    writer.f64(snapshot.flightTimeSeconds);
    writer.f64(snapshot.monotonicTimeSeconds);
    writer.f32(snapshot.deltaTimeSeconds);
    writer.f32(snapshot.timeAcceleration);
    writer.u8(snapshot.paused);
    writer.u8(snapshot.replaying);
    writer.u16(snapshot.reservedTime);
    writer.f64(snapshot.latitudeDeg);
    writer.f64(snapshot.longitudeDeg);
    writer.f64(snapshot.elevationMslM);
    appendVec3d(writer, snapshot.localPositionM);
    writer.f32(snapshot.pitchDeg);
    writer.f32(snapshot.rollDeg);
    writer.f32(snapshot.headingDegTrue);
    appendVec3f(writer, snapshot.linearVelocityLocalMps);
    appendVec3f(writer, snapshot.accelerationLocalMps2);
    appendVec3f(writer, snapshot.angularVelocityBodyRadps);
    writer.f32(snapshot.trueAirspeedMps);
    writer.f32(snapshot.groundSpeedMps);
    writer.f32(snapshot.angleOfAttackDeg);
    writer.f32(snapshot.sideslipDeg);
    writer.f32(snapshot.heightAglM);
    writer.f32(snapshot.normalLoadFactorG);
    writer.f32(snapshot.totalMassKg);
    writer.f32(snapshot.totalFuelMassKg);
    writer.f32(snapshot.flapRatio);
    writer.f32(snapshot.slatRatio);

    const auto& atmosphere = snapshot.atmosphere;
    writer.f32(atmosphere.temperatureK);
    writer.f32(atmosphere.staticPressurePa);
    writer.f32(atmosphere.densityKgM3);
    writer.f32(atmosphere.speedOfSoundMps);
    appendVec3f(writer, atmosphere.windLocalMps);
    writer.f32(atmosphere.thermalVerticalRateMps);
    writer.f32(atmosphere.precipitationRatio);
    writer.f32(atmosphere.snowRatio);
    writer.f32(atmosphere.hailRatio);
    writer.f32(atmosphere.gravityMps2);

    const auto& profile = atmosphere.profile;
    writer.u32(profile.temperatureLevelCount);
    writer.u32(profile.dewPointLevelCount);
    writer.u32(profile.windLevelCount);
    writer.u32(profile.reserved);
    appendFloatArray(writer, profile.temperatureAltitudeMslM);
    appendFloatArray(writer, profile.temperatureK);
    appendFloatArray(writer, profile.dewPointAltitudeMslM);
    appendFloatArray(writer, profile.dewPointK);
    appendFloatArray(writer, profile.windAltitudeMslM);
    appendFloatArray(writer, profile.windSpeedMps);
    appendFloatArray(writer, profile.windDirectionDegTrue);
    appendFloatArray(writer, profile.shearSpeedMps);
    appendFloatArray(writer, profile.shearDirectionDegTrue);
    appendFloatArray(writer, profile.turbulenceScale);

    writer.u32(snapshot.engineCount);
    for (const auto& engine : snapshot.engines) {
        writer.u8(engine.running);
        writer.u8(engine.reserved0);
        writer.u16(engine.reserved1);
        writer.f32(engine.n1Percent);
        writer.f32(engine.n2Percent);
        writer.f32(engine.fuelFlowKgps);
        writer.f32(engine.thrustN);
        writer.f32(engine.throttleRatio);
        writer.f32(engine.exhaustGasTemperatureK);
        writer.f32(engine.interTurbineTemperatureK);
        writer.f32(engine.jetwashVelocityMps);
        writer.f32(engine.exhaustVelocityMps);
    }
    writer.raw(snapshot.aircraftName.data(), snapshot.aircraftName.size());
    writer.raw(snapshot.aircraftIcao.data(), snapshot.aircraftIcao.size());
    writer.raw(snapshot.aircraftRelativePath.data(), snapshot.aircraftRelativePath.size());
}

bool readSnapshot(ByteReader& reader, engine::SimulatorSnapshot& snapshot) {
    if (!reader.u32(snapshot.schemaVersion) ||
        !reader.u32(snapshot.lifecycleFlags) ||
        !reader.u64(snapshot.sequenceNumber) ||
        !reader.u64(snapshot.validityMask) ||
        !reader.f64(snapshot.simulatorUptimeSeconds) ||
        !reader.f64(snapshot.flightTimeSeconds) ||
        !reader.f64(snapshot.monotonicTimeSeconds) ||
        !reader.f32(snapshot.deltaTimeSeconds) ||
        !reader.f32(snapshot.timeAcceleration) ||
        !reader.u8(snapshot.paused) ||
        !reader.u8(snapshot.replaying) ||
        !reader.u16(snapshot.reservedTime) ||
        !reader.f64(snapshot.latitudeDeg) ||
        !reader.f64(snapshot.longitudeDeg) ||
        !reader.f64(snapshot.elevationMslM) ||
        !readVec3d(reader, snapshot.localPositionM) ||
        !reader.f32(snapshot.pitchDeg) ||
        !reader.f32(snapshot.rollDeg) ||
        !reader.f32(snapshot.headingDegTrue) ||
        !readVec3f(reader, snapshot.linearVelocityLocalMps) ||
        !readVec3f(reader, snapshot.accelerationLocalMps2) ||
        !readVec3f(reader, snapshot.angularVelocityBodyRadps) ||
        !reader.f32(snapshot.trueAirspeedMps) ||
        !reader.f32(snapshot.groundSpeedMps) ||
        !reader.f32(snapshot.angleOfAttackDeg) ||
        !reader.f32(snapshot.sideslipDeg) ||
        !reader.f32(snapshot.heightAglM) ||
        !reader.f32(snapshot.normalLoadFactorG) ||
        !reader.f32(snapshot.totalMassKg) ||
        !reader.f32(snapshot.totalFuelMassKg) ||
        !reader.f32(snapshot.flapRatio) ||
        !reader.f32(snapshot.slatRatio)) return false;

    auto& atmosphere = snapshot.atmosphere;
    auto& profile = atmosphere.profile;
    if (!reader.f32(atmosphere.temperatureK) ||
        !reader.f32(atmosphere.staticPressurePa) ||
        !reader.f32(atmosphere.densityKgM3) ||
        !reader.f32(atmosphere.speedOfSoundMps) ||
        !readVec3f(reader, atmosphere.windLocalMps) ||
        !reader.f32(atmosphere.thermalVerticalRateMps) ||
        !reader.f32(atmosphere.precipitationRatio) ||
        !reader.f32(atmosphere.snowRatio) ||
        !reader.f32(atmosphere.hailRatio) ||
        !reader.f32(atmosphere.gravityMps2) ||
        !reader.u32(profile.temperatureLevelCount) ||
        !reader.u32(profile.dewPointLevelCount) ||
        !reader.u32(profile.windLevelCount) ||
        !reader.u32(profile.reserved) ||
        !readFloatArray(reader, profile.temperatureAltitudeMslM) ||
        !readFloatArray(reader, profile.temperatureK) ||
        !readFloatArray(reader, profile.dewPointAltitudeMslM) ||
        !readFloatArray(reader, profile.dewPointK) ||
        !readFloatArray(reader, profile.windAltitudeMslM) ||
        !readFloatArray(reader, profile.windSpeedMps) ||
        !readFloatArray(reader, profile.windDirectionDegTrue) ||
        !readFloatArray(reader, profile.shearSpeedMps) ||
        !readFloatArray(reader, profile.shearDirectionDegTrue) ||
        !readFloatArray(reader, profile.turbulenceScale) ||
        !reader.u32(snapshot.engineCount)) return false;

    if (profile.temperatureLevelCount > engine::kMaximumAtmosphereLevels ||
        profile.dewPointLevelCount > engine::kMaximumAtmosphereLevels ||
        profile.windLevelCount > engine::kMaximumAtmosphereLevels ||
        snapshot.engineCount > engine::kMaximumRecordedEngines) return false;

    for (auto& engine : snapshot.engines) {
        if (!reader.u8(engine.running) ||
            !reader.u8(engine.reserved0) ||
            !reader.u16(engine.reserved1) ||
            !reader.f32(engine.n1Percent) ||
            !reader.f32(engine.n2Percent) ||
            !reader.f32(engine.fuelFlowKgps) ||
            !reader.f32(engine.thrustN) ||
            !reader.f32(engine.throttleRatio) ||
            !reader.f32(engine.exhaustGasTemperatureK) ||
            !reader.f32(engine.interTurbineTemperatureK) ||
            !reader.f32(engine.jetwashVelocityMps) ||
            !reader.f32(engine.exhaustVelocityMps)) return false;
    }

    return reader.raw(snapshot.aircraftName.data(), snapshot.aircraftName.size()) &&
           reader.raw(snapshot.aircraftIcao.data(), snapshot.aircraftIcao.size()) &&
           reader.raw(snapshot.aircraftRelativePath.data(), snapshot.aircraftRelativePath.size());
}

std::string metadataText(const ReplayMetadata& metadata) {
    std::ostringstream stream;
    stream << "engine_build=" << metadata.engineBuild << '\n'
           << "git_revision=" << metadata.gitRevision << '\n'
           << "xplane_version=" << metadata.xplaneVersion << '\n'
           << "platform=" << metadata.platform << '\n'
           << "aircraft_name=" << metadata.aircraftName << '\n'
           << "aircraft_icao=" << metadata.aircraftIcao << '\n'
           << "aircraft_relative_path=" << metadata.aircraftRelativePath << '\n'
           << "coordinate_convention=" << metadata.coordinateConvention << '\n'
           << "scenario_seed=" << metadata.scenarioSeed << '\n';
    return stream.str();
}

ReplayMetadata parseMetadata(const std::string& text) {
    ReplayMetadata metadata;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "engine_build") metadata.engineBuild = value;
        else if (key == "git_revision") metadata.gitRevision = value;
        else if (key == "xplane_version") metadata.xplaneVersion = value;
        else if (key == "platform") metadata.platform = value;
        else if (key == "aircraft_name") metadata.aircraftName = value;
        else if (key == "aircraft_icao") metadata.aircraftIcao = value;
        else if (key == "aircraft_relative_path") metadata.aircraftRelativePath = value;
        else if (key == "coordinate_convention") metadata.coordinateConvention = value;
        else if (key == "scenario_seed") {
            try { metadata.scenarioSeed = static_cast<std::uint64_t>(std::stoull(value)); }
            catch (...) { metadata.scenarioSeed = 0; }
        }
    }
    return metadata;
}

void writeFileHeader(std::ofstream& stream) {
    ByteWriter writer;
    writer.raw(kMagic.data(), kMagic.size());
    writer.u32(kReplayContainerVersion);
    writer.u32(engine::kSimulatorSnapshotSchemaVersion);
    writer.u32(kEndianMarker);
    writer.u32(0u);
    writeBytes(stream, writer.bytes());
}

void writeChunk(std::ofstream& stream,
                std::uint32_t type,
                std::uint32_t schemaVersion,
                std::uint64_t firstSequence,
                std::uint64_t lastSequence,
                const std::vector<std::uint8_t>& payload) {
    ByteWriter header;
    header.u32(type);
    header.u32(schemaVersion);
    header.u64(static_cast<std::uint64_t>(payload.size()));
    header.u64(firstSequence);
    header.u64(lastSequence);
    header.u32(crc32(payload.data(), payload.size()));
    header.u32(0u);
    writeBytes(stream, header.bytes());
    writeBytes(stream, payload);
}

bool readFileHeader(std::ifstream& stream, std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!readExact(stream, bytes, 24)) {
        error = "Replay file is truncated before its header";
        return false;
    }
    ByteReader reader(bytes);
    std::array<char, 8> magic {};
    std::uint32_t containerVersion = 0;
    std::uint32_t snapshotVersion = 0;
    std::uint32_t endian = 0;
    std::uint32_t reserved = 0;
    if (!reader.raw(magic.data(), magic.size()) ||
        !reader.u32(containerVersion) ||
        !reader.u32(snapshotVersion) ||
        !reader.u32(endian) ||
        !reader.u32(reserved)) {
        error = "Replay header could not be decoded";
        return false;
    }
    if (magic != kMagic) { error = "Replay magic is not FFATRPL1"; return false; }
    if (containerVersion != kReplayContainerVersion) {
        error = "Unsupported replay container version"; return false;
    }
    if (snapshotVersion != engine::kSimulatorSnapshotSchemaVersion) {
        error = "Unsupported simulator snapshot schema version"; return false;
    }
    if (endian != kEndianMarker) { error = "Replay endianness is unsupported"; return false; }
    return true;
}

struct ChunkHeader {
    std::uint32_t type = 0;
    std::uint32_t schemaVersion = 0;
    std::uint64_t payloadBytes = 0;
    std::uint64_t firstSequence = 0;
    std::uint64_t lastSequence = 0;
    std::uint32_t checksum = 0;
    std::uint32_t compression = 0;
};

bool readChunkHeader(std::ifstream& stream, ChunkHeader& header, bool& endOfFile) {
    endOfFile = false;
    std::vector<std::uint8_t> bytes(40);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() == 0 && stream.eof()) { endOfFile = true; return true; }
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) return false;
    ByteReader reader(bytes);
    return reader.u32(header.type) && reader.u32(header.schemaVersion) &&
           reader.u64(header.payloadBytes) && reader.u64(header.firstSequence) &&
           reader.u64(header.lastSequence) && reader.u32(header.checksum) &&
           reader.u32(header.compression);
}

}  // namespace

class ReplayRecorder::Impl {
public:
    bool start(const std::filesystem::path& outputPath,
               ReplayMetadata metadata,
               std::string* error) {
        if (running_.load()) {
            if (error) *error = "Recorder is already running";
            return false;
        }
        std::error_code directoryError;
        if (!outputPath.parent_path().empty()) {
            std::filesystem::create_directories(outputPath.parent_path(), directoryError);
            if (directoryError) {
                if (error) *error = "Could not create recording directory: " + directoryError.message();
                return false;
            }
        }
        stream_.open(outputPath, std::ios::binary | std::ios::trunc);
        if (!stream_) {
            if (error) *error = "Could not open replay output file";
            return false;
        }
        path_ = outputPath;
        metadata_ = std::move(metadata);
        writeIndex_.store(0);
        readIndex_.store(0);
        accepted_.store(0);
        dropped_.store(0);
        firstSequence_ = 0;
        lastSequence_ = 0;
        firstSimulatorTime_ = 0.0;
        lastSimulatorTime_ = 0.0;
        lastError_.clear();

        writeFileHeader(stream_);
        const std::string text = metadataText(metadata_);
        writeChunk(stream_, kChunkMetadata, 1u, 0u, 0u,
                   std::vector<std::uint8_t>(text.begin(), text.end()));
        if (!stream_) {
            if (error) *error = "Could not write replay header";
            stream_.close();
            return false;
        }
        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this] { workerLoop(); });
        return true;
    }

    bool tryPush(const engine::SimulatorSnapshot& snapshot) {
        if (!running_.load(std::memory_order_acquire)) return false;
        const std::size_t write = writeIndex_.load(std::memory_order_relaxed);
        const std::size_t next = (write + 1) % kQueueCapacity;
        if (next == readIndex_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        queue_[write] = snapshot;
        writeIndex_.store(next, std::memory_order_release);
        accepted_.fetch_add(1, std::memory_order_relaxed);
        wake_.notify_one();
        return true;
    }

    bool stop(std::string* error) {
        running_.store(false, std::memory_order_release);
        wake_.notify_one();
        if (worker_.joinable()) worker_.join();
        if (stream_.is_open()) {
            ByteWriter endPayload;
            endPayload.u64(accepted_.load());
            endPayload.u64(dropped_.load());
            endPayload.u64(firstSequence_);
            endPayload.u64(lastSequence_);
            endPayload.f64(firstSimulatorTime_);
            endPayload.f64(lastSimulatorTime_);
            writeChunk(stream_, kChunkEnd, 1u, firstSequence_, lastSequence_, endPayload.bytes());
            stream_.flush();
            if (!stream_ && lastError_.empty()) lastError_ = "Replay stream failed while finalizing";
            stream_.close();
        }
        if (error && !lastError_.empty()) *error = lastError_;
        return lastError_.empty();
    }

    void workerLoop() {
        std::vector<engine::SimulatorSnapshot> block;
        block.reserve(kSnapshotBlockCount);
        while (running_.load(std::memory_order_acquire) ||
               readIndex_.load(std::memory_order_acquire) != writeIndex_.load(std::memory_order_acquire)) {
            block.clear();
            while (block.size() < kSnapshotBlockCount) {
                const std::size_t read = readIndex_.load(std::memory_order_relaxed);
                if (read == writeIndex_.load(std::memory_order_acquire)) break;
                block.push_back(queue_[read]);
                readIndex_.store((read + 1) % kQueueCapacity, std::memory_order_release);
            }
            if (block.empty()) {
                std::unique_lock<std::mutex> lock(wakeMutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(10));
                continue;
            }
            ByteWriter payload;
            payload.u32(static_cast<std::uint32_t>(block.size()));
            for (const auto& snapshot : block) appendSnapshot(payload, snapshot);
            const std::uint64_t first = block.front().sequenceNumber;
            const std::uint64_t last = block.back().sequenceNumber;
            if (firstSequence_ == 0) {
                firstSequence_ = first;
                firstSimulatorTime_ = block.front().simulatorUptimeSeconds;
            }
            lastSequence_ = last;
            lastSimulatorTime_ = block.back().simulatorUptimeSeconds;
            writeChunk(stream_, kChunkSnapshotBlock, 1u, first, last, payload.bytes());
            if (!stream_) {
                lastError_ = "Replay stream failed while writing snapshot block";
                running_.store(false, std::memory_order_release);
                break;
            }
        }
    }

    std::array<engine::SimulatorSnapshot, kQueueCapacity> queue_ {};
    std::atomic<std::size_t> writeIndex_ {0};
    std::atomic<std::size_t> readIndex_ {0};
    std::atomic<bool> running_ {false};
    std::atomic<std::uint64_t> accepted_ {0};
    std::atomic<std::uint64_t> dropped_ {0};
    std::thread worker_;
    std::condition_variable wake_;
    std::mutex wakeMutex_;
    std::ofstream stream_;
    std::filesystem::path path_;
    ReplayMetadata metadata_;
    std::string lastError_;
    std::uint64_t firstSequence_ = 0;
    std::uint64_t lastSequence_ = 0;
    double firstSimulatorTime_ = 0.0;
    double lastSimulatorTime_ = 0.0;
};

ReplayRecorder::ReplayRecorder() : impl_(std::make_unique<Impl>()) {}
ReplayRecorder::~ReplayRecorder() { stop(); }
bool ReplayRecorder::start(const std::filesystem::path& outputPath,
                           ReplayMetadata metadata,
                           std::string* error) {
    return impl_->start(outputPath, std::move(metadata), error);
}
bool ReplayRecorder::tryPush(const engine::SimulatorSnapshot& snapshot) {
    return impl_->tryPush(snapshot);
}
bool ReplayRecorder::stop(std::string* error) { return impl_->stop(error); }
bool ReplayRecorder::recording() const { return impl_->running_.load(std::memory_order_acquire); }
std::uint64_t ReplayRecorder::acceptedSnapshotCount() const { return impl_->accepted_.load(); }
std::uint64_t ReplayRecorder::droppedSnapshotCount() const { return impl_->dropped_.load(); }
std::filesystem::path ReplayRecorder::outputPath() const { return impl_->path_; }

ReplayReadResult readReplayFile(const std::filesystem::path& path,
                                std::size_t maximumSnapshots) {
    ReplayReadResult result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { result.error = "Could not open replay file"; return result; }
    if (!readFileHeader(stream, result.error)) return result;

    while (true) {
        ChunkHeader header;
        bool endOfFile = false;
        if (!readChunkHeader(stream, header, endOfFile)) {
            result.error = "Replay is truncated in a chunk header";
            return result;
        }
        if (endOfFile) break;
        if (header.payloadBytes > kMaximumChunkBytes || header.compression != 0u) {
            result.error = "Replay contains an unsupported or oversized chunk";
            return result;
        }
        std::vector<std::uint8_t> payload;
        if (!readExact(stream, payload, static_cast<std::size_t>(header.payloadBytes))) {
            result.error = "Replay is truncated in a chunk payload";
            return result;
        }
        if (crc32(payload.data(), payload.size()) != header.checksum) {
            result.error = "Replay chunk checksum mismatch";
            return result;
        }

        if (header.type == kChunkMetadata) {
            result.metadata = parseMetadata(std::string(payload.begin(), payload.end()));
        } else if (header.type == kChunkSnapshotBlock) {
            ByteReader reader(payload);
            std::uint32_t count = 0;
            if (!reader.u32(count)) { result.error = "Snapshot block is missing its count"; return result; }
            if (count > maximumSnapshots - result.snapshots.size()) {
                result.error = "Replay exceeds configured snapshot limit";
                return result;
            }
            for (std::uint32_t index = 0; index < count; ++index) {
                engine::SimulatorSnapshot snapshot;
                if (!readSnapshot(reader, snapshot) ||
                    snapshot.schemaVersion != engine::kSimulatorSnapshotSchemaVersion) {
                    result.error = "Snapshot block contains invalid schema data";
                    return result;
                }
                result.snapshots.push_back(snapshot);
            }
            if (!reader.finished()) {
                result.error = "Snapshot block has trailing or malformed bytes";
                return result;
            }
        } else if (header.type == kChunkEnd) {
            ByteReader reader(payload);
            if (!reader.u64(result.summary.snapshotCount) ||
                !reader.u64(result.summary.droppedSnapshotCount) ||
                !reader.u64(result.summary.firstSequence) ||
                !reader.u64(result.summary.lastSequence) ||
                !reader.f64(result.summary.firstSimulatorTimeSeconds) ||
                !reader.f64(result.summary.lastSimulatorTimeSeconds) ||
                !reader.finished()) {
                result.error = "Replay end chunk is malformed";
                return result;
            }
            result.summary.cleanEndChunk = true;
            break;
        }
    }

    if (!result.summary.cleanEndChunk) {
        result.error = "Replay ended without a clean end chunk";
        return result;
    }
    if (result.summary.snapshotCount != result.snapshots.size()) {
        result.error = "Replay snapshot count does not match its end chunk";
        return result;
    }
    result.ok = true;
    return result;
}

bool writeReplaySummary(const ReplayReadResult& replay,
                        const std::filesystem::path& outputPath,
                        std::string* error) {
    std::error_code directoryError;
    if (!outputPath.parent_path().empty()) {
        std::filesystem::create_directories(outputPath.parent_path(), directoryError);
        if (directoryError) {
            if (error) *error = directoryError.message();
            return false;
        }
    }
    std::ofstream stream(outputPath, std::ios::trunc);
    if (!stream) {
        if (error) *error = "Could not open replay summary output";
        return false;
    }
    stream << "FFAtmo Replay Summary\n"
           << "status=" << (replay.ok ? "OK" : "ERROR") << '\n'
           << "error=" << replay.error << '\n'
           << "engine_build=" << replay.metadata.engineBuild << '\n'
           << "git_revision=" << replay.metadata.gitRevision << '\n'
           << "xplane_version=" << replay.metadata.xplaneVersion << '\n'
           << "platform=" << replay.metadata.platform << '\n'
           << "aircraft_name=" << replay.metadata.aircraftName << '\n'
           << "aircraft_icao=" << replay.metadata.aircraftIcao << '\n'
           << "aircraft_relative_path=" << replay.metadata.aircraftRelativePath << '\n'
           << "snapshot_count=" << replay.summary.snapshotCount << '\n'
           << "dropped_snapshot_count=" << replay.summary.droppedSnapshotCount << '\n'
           << "first_sequence=" << replay.summary.firstSequence << '\n'
           << "last_sequence=" << replay.summary.lastSequence << '\n'
           << std::fixed << std::setprecision(6)
           << "first_simulator_time=" << replay.summary.firstSimulatorTimeSeconds << '\n'
           << "last_simulator_time=" << replay.summary.lastSimulatorTimeSeconds << '\n'
           << "clean_end_chunk=" << (replay.summary.cleanEndChunk ? 1 : 0) << '\n';
    if (!stream) {
        if (error) *error = "Could not write replay summary";
        return false;
    }
    return true;
}

}  // namespace ffatmo::diagnostics

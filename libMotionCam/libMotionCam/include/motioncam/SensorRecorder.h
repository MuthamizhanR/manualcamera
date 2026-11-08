#ifndef SensorRecorder_hpp
#define SensorRecorder_hpp

#include <android/sensor.h>
#include <android/looper.h>
#include <json11/json11.hpp>
#include <vector>
#include <mutex>
#include <atomic>

namespace motioncam {

    // Must match reference/motioncam-decoder 24-byte layout
    struct MotionSample {
        int64_t timestampNs = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint32_t reserved = 0;
    };

    class SensorRecorder {
    public:
        SensorRecorder();
        ~SensorRecorder();

        static SensorRecorder& get();

        void start();
        void stop();

        std::vector<MotionSample> getGyroSamples(int64_t startTsNs = 0, int64_t endTsNs = 0x7FFFFFFFFFFFFFFF) const;
        std::vector<MotionSample> getAccelSamples(int64_t startTsNs = 0, int64_t endTsNs = 0x7FFFFFFFFFFFFFFF) const;

        json11::Json::array getGyroJson() const;
        json11::Json::array getAccelJson() const;

        void clear();

    private:
        static int sensorCallback(int fd, int events, void* data);
        void onSensorEvent(const ASensorEvent& event);

        ASensorManager* mSensorManager;
        ALooper* mLooper;
        ASensorEventQueue* mEventQueue;

        const ASensor* mGyroSensor;
        const ASensor* mAccelSensor;

        static constexpr size_t MAX_SAMPLES = 6000;

        std::vector<MotionSample> mGyroSamples;
        std::vector<MotionSample> mAccelSamples;

        mutable std::mutex mMutex;
        std::atomic<bool> mIsRecording;
    };
}

#endif // SensorRecorder_hpp

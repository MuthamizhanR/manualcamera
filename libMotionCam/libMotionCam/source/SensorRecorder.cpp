#include "motioncam/SensorRecorder.h"
#include "motioncam/Logger.h"
#include <algorithm>
#include <dlfcn.h>
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SensorRecorder", __VA_ARGS__)

namespace motioncam {

    SensorRecorder& SensorRecorder::get() {
        static SensorRecorder instance;
        return instance;
    }

    SensorRecorder::SensorRecorder()
        : mThread(),
          mInitDone(false),
          mSensorManager(nullptr),
          mLooper(nullptr),
          mEventQueue(nullptr),
          mGyroSensor(nullptr),
          mAccelSensor(nullptr),
          mIsRecording(false) {
        typedef ASensorManager* (*PFN_ASensorManager_getInstanceForPackage)(const char*);
        static auto pfnGetInstanceForPackage = (PFN_ASensorManager_getInstanceForPackage) dlsym(RTLD_DEFAULT, "ASensorManager_getInstanceForPackage");
        if (pfnGetInstanceForPackage) {
            mSensorManager = pfnGetInstanceForPackage("com.motioncam");
        } else {
            mSensorManager = ASensorManager_getInstance();
        }

        if (mSensorManager) {
            mGyroSensor = ASensorManager_getDefaultSensor(mSensorManager, ASENSOR_TYPE_GYROSCOPE);
            mAccelSensor = ASensorManager_getDefaultSensor(mSensorManager, ASENSOR_TYPE_ACCELEROMETER);
        } else {
            LOGE("SensorRecorder: Failed to get ASensorManager");
        }

        mThread = std::thread(&SensorRecorder::looperThread, this);

        std::unique_lock<std::mutex> lock(mMutex);
        mInitCond.wait(lock, [this]() { return mInitDone; });
    }

    SensorRecorder::~SensorRecorder() {
        stop();
        if (mLooper) {
            ALooper_wake(mLooper);
        }
        if (mThread.joinable()) {
            mThread.join();
        }
    }

    void SensorRecorder::looperThread() {
        if (!mSensorManager) {
            std::lock_guard<std::mutex> lock(mMutex);
            mInitDone = true;
            mInitCond.notify_one();
            return;
        }

        mLooper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
        mEventQueue = ASensorManager_createEventQueue(mSensorManager, mLooper, 3, sensorCallback, this);

        {
            std::lock_guard<std::mutex> lock(mMutex);
            mInitDone = true;
        }
        mInitCond.notify_one();

        while (true) {
            int ident = ALooper_pollAll(-1, nullptr, nullptr, nullptr);
            if (ident == ALOOPER_POLL_WAKE) {
                break; // Wake means exit thread
            }
        }

        if (mEventQueue) {
            ASensorManager_destroyEventQueue(mSensorManager, mEventQueue);
            mEventQueue = nullptr;
        }
    }

    void SensorRecorder::start() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mIsRecording || !mEventQueue) return;

        mGyroSamples.clear();
        mAccelSamples.clear();

        if (mGyroSensor) {
            ASensorEventQueue_enableSensor(mEventQueue, mGyroSensor);
            ASensorEventQueue_setEventRate(mEventQueue, mGyroSensor, 1000000 / 200); // 200Hz
        }
        if (mAccelSensor) {
            ASensorEventQueue_enableSensor(mEventQueue, mAccelSensor);
            ASensorEventQueue_setEventRate(mEventQueue, mAccelSensor, 1000000 / 200); // 200Hz
        }

        mIsRecording = true;
    }

    void SensorRecorder::stop() {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mIsRecording || !mEventQueue) return;

        if (mGyroSensor) {
            ASensorEventQueue_disableSensor(mEventQueue, mGyroSensor);
        }
        if (mAccelSensor) {
            ASensorEventQueue_disableSensor(mEventQueue, mAccelSensor);
        }

        mIsRecording = false;
    }

    void SensorRecorder::clear() {
        std::lock_guard<std::mutex> lock(mMutex);
        mGyroSamples.clear();
        mAccelSamples.clear();
    }

    int SensorRecorder::sensorCallback(int fd, int events, void* data) {
        SensorRecorder* recorder = static_cast<SensorRecorder*>(data);

        ASensorEvent event;
        while (recorder->mEventQueue && ASensorEventQueue_getEvents(recorder->mEventQueue, &event, 1) > 0) {
            recorder->onSensorEvent(event);
        }

        return 1;
    }

    void SensorRecorder::onSensorEvent(const ASensorEvent& event) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mIsRecording) return;

        MotionSample sample;
        sample.timestampNs = event.timestamp;

        if (event.type == ASENSOR_TYPE_GYROSCOPE) {
            sample.x = event.vector.v[0];
            sample.y = event.vector.v[1];
            sample.z = event.vector.v[2];

            while (mGyroSamples.size() >= MAX_SAMPLES) {
                mGyroSamples.pop_front();
            }
            mGyroSamples.push_back(sample);
        } else if (event.type == ASENSOR_TYPE_ACCELEROMETER) {
            sample.x = event.vector.v[0];
            sample.y = event.vector.v[1];
            sample.z = event.vector.v[2];

            while (mAccelSamples.size() >= MAX_SAMPLES) {
                mAccelSamples.pop_front();
            }
            mAccelSamples.push_back(sample);
        }
    }

    std::vector<MotionSample> SensorRecorder::getGyroSamples(int64_t startTsNs, int64_t endTsNs) const {
        std::lock_guard<std::mutex> lock(mMutex);
        std::vector<MotionSample> result;
        for (const auto& sample : mGyroSamples) {
            if (sample.timestampNs >= startTsNs && sample.timestampNs <= endTsNs) {
                result.push_back(sample);
            }
        }
        return result;
    }

    std::vector<MotionSample> SensorRecorder::getAccelSamples(int64_t startTsNs, int64_t endTsNs) const {
        std::lock_guard<std::mutex> lock(mMutex);
        std::vector<MotionSample> result;
        for (const auto& sample : mAccelSamples) {
            if (sample.timestampNs >= startTsNs && sample.timestampNs <= endTsNs) {
                result.push_back(sample);
            }
        }
        return result;
    }

    json11::Json::array SensorRecorder::getGyroJson() const {
        std::lock_guard<std::mutex> lock(mMutex);
        json11::Json::array arr;
        for (const auto& sample : mGyroSamples) {
            json11::Json::array pt = {
                std::to_string(sample.timestampNs),
                sample.x, sample.y, sample.z
            };
            arr.push_back(pt);
        }
        return arr;
    }

    json11::Json::array SensorRecorder::getAccelJson() const {
        std::lock_guard<std::mutex> lock(mMutex);
        json11::Json::array arr;
        for (const auto& sample : mAccelSamples) {
            json11::Json::array pt = {
                std::to_string(sample.timestampNs),
                sample.x, sample.y, sample.z
            };
            arr.push_back(pt);
        }
        return arr;
    }

}

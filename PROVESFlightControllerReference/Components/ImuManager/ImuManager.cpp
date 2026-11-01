// ======================================================================
// \title  ImuManager.cpp
// \brief  cpp file for ImuManager component implementation class
// ======================================================================

#include "PROVESFlightControllerReference/Components/ImuManager/ImuManager.hpp"

#include <Fw/Types/Assert.hpp>
#include <cmath>
#include <errno.h>

namespace {
constexpr double PI = 3.14159265358979323846;
constexpr double RAD_TO_DEG = 180.0 / PI;
}  // namespace

namespace Components {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

ImuManager ::ImuManager(const char* const compName) : ImuManagerComponentBase(compName) {}

ImuManager ::~ImuManager() {}

// ----------------------------------------------------------------------
// Public helper methods
// ----------------------------------------------------------------------
void ImuManager ::configure(const struct device* lis2mdl, const struct device* lsm6dso) {
    this->m_lis2mdl = lis2mdl;
    this->m_lsm6dso = lsm6dso;

    struct sensor_value magn_odr = this->getMagnetometerSamplingFrequency();
    struct sensor_value accel_odr = this->getAccelerometerSamplingFrequency();
    struct sensor_value gyro_odr = this->getGyroscopeSamplingFrequency();
    this->configureSensors(magn_odr, accel_odr, gyro_odr);
}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void ImuManager ::run_handler(FwIndexType portNum, U32 context) {
    Fw::Success condition;  // Ignoring for now
    Drv::Acceleration acceleration = this->accelerationGet_handler(0, condition);
    Drv::AngularVelocity angular_velocity = this->angularVelocityGet_handler(0, condition);
    Drv::MagneticField magnetic_field = this->magneticFieldGet_handler(0, condition);

    // Read IMU die temp
    F32 die_temp_c = 0.0f;
    if (this->dieTemperatureGet(die_temp_c)) {
        this->tlmWrite_DieTempC(die_temp_c);
    }

    // Check if parameters have changed, and reconfigure sensors if they have
    struct sensor_value magn_odr = this->getMagnetometerSamplingFrequency();
    struct sensor_value accel_odr = this->getAccelerometerSamplingFrequency();
    struct sensor_value gyro_odr = this->getGyroscopeSamplingFrequency();
    if (!this->sensorValuesEqual(&magn_odr, &this->m_curr_magn_odr) ||
        !this->sensorValuesEqual(&accel_odr, &this->m_curr_accel_odr) ||
        !this->sensorValuesEqual(&gyro_odr, &this->m_curr_gyro_odr)) {
        this->configureSensors(magn_odr, accel_odr, gyro_odr);
    }
}

Drv::Acceleration ImuManager ::accelerationGet_handler(FwIndexType portNum, Fw::Success& condition) {
    condition = Fw::Success::FAILURE;

    if (!device_is_ready(this->m_lsm6dso)) {
        this->log_WARNING_HI_Lsm6dsoDeviceNotReady();
        return Drv::Acceleration(0.0, 0.0, 0.0);
    }
    this->log_WARNING_HI_Lsm6dsoDeviceNotReady_ThrottleClear();

    struct sensor_value x, y, z;
    if (!this->readVector(this->m_lsm6dso, SENSOR_CHAN_ACCEL_XYZ, SENSOR_CHAN_ACCEL_X, SENSOR_CHAN_ACCEL_Y,
                          SENSOR_CHAN_ACCEL_Z, ImuSensor::LSM6DSO_ACCEL, x, y, z)) {
        return Drv::Acceleration(0.0, 0.0, 0.0);
    }

    this->applyAxisOrientation(x, y, z);

    Drv::Acceleration acceleration =
        Drv::Acceleration(sensor_value_to_double(&x), sensor_value_to_double(&y), sensor_value_to_double(&z));

    this->tlmWrite_Acceleration(acceleration);

    condition = Fw::Success::SUCCESS;
    return acceleration;
}

Drv::AngularVelocity ImuManager ::angularVelocityGet_handler(FwIndexType portNum, Fw::Success& condition) {
    condition = Fw::Success::FAILURE;

    if (!device_is_ready(this->m_lsm6dso)) {
        this->log_WARNING_HI_Lsm6dsoDeviceNotReady();
        return Drv::AngularVelocity(0.0, 0.0, 0.0);
    }
    this->log_WARNING_HI_Lsm6dsoDeviceNotReady_ThrottleClear();

    struct sensor_value x, y, z;
    if (!this->readVector(this->m_lsm6dso, SENSOR_CHAN_GYRO_XYZ, SENSOR_CHAN_GYRO_X, SENSOR_CHAN_GYRO_Y,
                          SENSOR_CHAN_GYRO_Z, ImuSensor::LSM6DSO_GYRO, x, y, z)) {
        return Drv::AngularVelocity(0.0, 0.0, 0.0);
    }

    this->applyAxisOrientation(x, y, z);

    Drv::AngularVelocity angular_velocity =
        Drv::AngularVelocity(sensor_value_to_double(&x), sensor_value_to_double(&y), sensor_value_to_double(&z));

    this->tlmWrite_AngularVelocity(angular_velocity);

    condition = Fw::Success::SUCCESS;
    return angular_velocity;
}

F64 ImuManager ::angularVelocityMagnitudeGet_handler(FwIndexType portNum,
                                                     Fw::Success& condition,
                                                     const Components::AngularUnit& unit) {
    condition = Fw::Success::FAILURE;

    // Get angular velocity
    Drv::AngularVelocity angular_velocity = this->angularVelocityGet_handler(0, condition);

    // Compute magnitude
    F64 magnitude = std::sqrt(angular_velocity.get_x() * angular_velocity.get_x() +
                              angular_velocity.get_y() * angular_velocity.get_y() +
                              angular_velocity.get_z() * angular_velocity.get_z());

    // Convert to requested unit
    if (unit == AngularUnit::DEG_PER_SEC) {
        return magnitude * RAD_TO_DEG;
    }

    return magnitude;
}

Drv::MagneticField ImuManager ::magneticFieldGet_handler(FwIndexType portNum, Fw::Success& condition) {
    condition = Fw::Success::FAILURE;

    if (!device_is_ready(this->m_lis2mdl)) {
        this->log_WARNING_HI_Lis2mdlDeviceNotReady();
        return Drv::MagneticField(0.0, 0.0, 0.0, Fw::TimeValue());
    }
    this->log_WARNING_HI_Lis2mdlDeviceNotReady_ThrottleClear();

    struct sensor_value x, y, z;
    if (!this->readVector(this->m_lis2mdl, SENSOR_CHAN_MAGN_XYZ, SENSOR_CHAN_MAGN_X, SENSOR_CHAN_MAGN_Y,
                          SENSOR_CHAN_MAGN_Z, ImuSensor::LIS2MDL_MAGN, x, y, z)) {
        return Drv::MagneticField(0.0, 0.0, 0.0, Fw::TimeValue());
    }

    this->applyAxisOrientation(x, y, z);

    Fw::Time t = this->getTime();
    Fw::TimeValue timestamp = Fw::TimeValue(t.getTimeBase(), t.getContext(), t.getSeconds(), t.getUSeconds());

    Drv::MagneticField magnetic_field = Drv::MagneticField(sensor_value_to_double(&x), sensor_value_to_double(&y),
                                                           sensor_value_to_double(&z), timestamp);

    this->tlmWrite_MagneticField(magnetic_field);

    condition = Fw::Success::SUCCESS;
    return magnetic_field;
}

Fw::TimeIntervalValue ImuManager ::magneticFieldSamplingPeriodGet_handler(FwIndexType portNum, Fw::Success& condition) {
    condition = Fw::Success::FAILURE;

    // Get sampling frequency in Hz
    struct sensor_value odr = getMagnetometerSamplingFrequency();
    double frequency_hz = sensor_value_to_double(&odr);
    if (frequency_hz <= 0.0) {
        this->log_WARNING_LO_MagnetometerSamplingFrequencyZeroHz();
        return Fw::TimeIntervalValue();
    }
    this->log_WARNING_LO_MagnetometerSamplingFrequencyZeroHz_ThrottleClear();

    // Convert sampling frequency to microseconds
    double period_seconds = 1.0 / frequency_hz;
    U32 period_useconds = static_cast<U32>(period_seconds * 1e6);

    condition = Fw::Success::SUCCESS;
    return Fw::TimeIntervalValue(0, period_useconds);
}

// ----------------------------------------------------------------------
//  Private helper methods
// ----------------------------------------------------------------------

bool ImuManager ::readVector(const struct device* dev,
                             enum sensor_channel fetchChan,
                             enum sensor_channel chanX,
                             enum sensor_channel chanY,
                             enum sensor_channel chanZ,
                             Components::ImuSensor::T sensor,
                             struct sensor_value& x,
                             struct sensor_value& y,
                             struct sensor_value& z) {

    // Get sensor channel
    const int fetchStatus = sensor_sample_fetch_chan(dev, fetchChan);
    if (fetchStatus != 0) {
        this->log_WARNING_HI_SensorReadFailed(sensor, static_cast<I32>(-fetchStatus));
        return false;
    }

    // Read individual axis channels
    const int statusX = sensor_channel_get(dev, chanX, &x);
    const int statusY = sensor_channel_get(dev, chanY, &y);
    const int statusZ = sensor_channel_get(dev, chanZ, &z);

    if (statusX != 0 || statusY != 0 || statusZ != 0) {
        const int firstFailure = (statusX != 0) ? statusX : ((statusY != 0) ? statusY : statusZ);
        this->log_WARNING_HI_SensorReadFailed(sensor, static_cast<I32>(-firstFailure));
        return false;
    }

    // Clear warning throttle on a successful read
    this->log_WARNING_HI_SensorReadFailed_ThrottleClear();
    return true;
}

bool ImuManager ::dieTemperatureGet(F32& temperatureC) {
    temperatureC = 0.0f;

    // Exit if reading die temp isn't configured
    if (!this->m_dieTempSupported) {
        return false;
    }

    // Exit if IMU isn't readable
    if (!device_is_ready(this->m_lsm6dso)) {
        this->log_WARNING_HI_Lsm6dsoDeviceNotReady();
        return false;
    }

    const int fetchStatus = sensor_sample_fetch_chan(this->m_lsm6dso, SENSOR_CHAN_DIE_TEMP);
    if (fetchStatus != 0) {
        
        // Enable fast exit if reading the die temp isn't configured.
        if (fetchStatus == -ENOTSUP || fetchStatus == -ENOSYS) {
            this->m_dieTempSupported = false;
            this->log_WARNING_LO_DieTemperatureUnavailable(static_cast<I32>(-fetchStatus));
            return false;
        }

        this->log_WARNING_HI_SensorReadFailed(ImuSensor::LSM6DSO_TEMP, static_cast<I32>(-fetchStatus));
        return false;
    }

    // Read temperature
    struct sensor_value t;
    const int getStatus = sensor_channel_get(this->m_lsm6dso, SENSOR_CHAN_DIE_TEMP, &t);
    if (getStatus != 0) {
        if (getStatus == -ENOTSUP || getStatus == -ENOSYS) {
            this->m_dieTempSupported = false;
            this->log_WARNING_LO_DieTemperatureUnavailable(static_cast<I32>(-getStatus));
            return false;
        }
        this->log_WARNING_HI_SensorReadFailed(ImuSensor::LSM6DSO_TEMP, static_cast<I32>(-getStatus));
        return false;
    }

    temperatureC = static_cast<F32>(sensor_value_to_double(&t));
    return true;
}

void ImuManager ::configureSensors(struct sensor_value& magn, struct sensor_value& accel, struct sensor_value& gyro) {
    // Configure the lis2mdl
    if (sensor_attr_set(this->m_lis2mdl, SENSOR_CHAN_MAGN_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &magn) != 0) {
        this->log_WARNING_HI_MagnetometerSamplingFrequencyNotConfigured();
    } else {
        this->m_curr_magn_odr = magn;
    }

    // Configure the lsm6dso
    if (sensor_attr_set(this->m_lsm6dso, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &accel) != 0) {
        this->log_WARNING_HI_AccelerometerSamplingFrequencyNotConfigured();
    } else {
        this->m_curr_accel_odr = accel;
    }
    if (sensor_attr_set(this->m_lsm6dso, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &gyro) != 0) {
        this->log_WARNING_HI_GyroscopeSamplingFrequencyNotConfigured();
    } else {
        this->m_curr_gyro_odr = gyro;
    }
}

void ImuManager ::applyAxisOrientation(struct sensor_value& x, struct sensor_value& y, struct sensor_value& z) {
    Fw::ParamValid valid;
    Components::AxisOrientation::T orientation = this->paramGet_AXIS_ORIENTATION(valid);

    this->tlmWrite_AxisOrientation(orientation);

    switch (orientation) {
        case Components::AxisOrientation::ROTATED_90_DEG_CW: {
            const struct sensor_value temp_x = x;
            x = y;
            sensor_value_from_double(&y, -sensor_value_to_double(&temp_x));
            return;
        }
        case Components::AxisOrientation::ROTATED_90_DEG_CCW: {
            const struct sensor_value temp_x = x;
            sensor_value_from_double(&x, -sensor_value_to_double(&y));
            y = temp_x;
            return;
        }
        case Components::AxisOrientation::ROTATED_180_DEG: {
            sensor_value_from_double(&x, -sensor_value_to_double(&x));
            sensor_value_from_double(&y, -sensor_value_to_double(&y));
            return;
        }
        case Components::AxisOrientation::STANDARD: {
            return;
        }
    }
}

struct sensor_value ImuManager ::getAccelerometerSamplingFrequency() {
    Fw::ParamValid valid;
    Components::Lsm6dsoSamplingFrequency freqParam = this->paramGet_ACCELEROMETER_SAMPLING_FREQUENCY(valid);

    this->tlmWrite_AccelerometerSamplingFrequency(freqParam);

    return this->getLsm6dsoSamplingFrequency(freqParam);
}

struct sensor_value ImuManager ::getGyroscopeSamplingFrequency() {
    Fw::ParamValid valid;
    Components::Lsm6dsoSamplingFrequency freqParam = this->paramGet_GYROSCOPE_SAMPLING_FREQUENCY(valid);

    this->tlmWrite_GyroscopeSamplingFrequency(freqParam);

    return this->getLsm6dsoSamplingFrequency(freqParam);
}

struct sensor_value ImuManager ::getLsm6dsoSamplingFrequency(Components::Lsm6dsoSamplingFrequency freqParam) {
    switch (freqParam) {
        case Components::Lsm6dsoSamplingFrequency::SF_12_5Hz:
            return sensor_value{12, 500000};
        case Components::Lsm6dsoSamplingFrequency::SF_26Hz:
            return sensor_value{26, 0};
        case Components::Lsm6dsoSamplingFrequency::SF_52Hz:
            return sensor_value{52, 0};
        case Components::Lsm6dsoSamplingFrequency::SF_104Hz:
            return sensor_value{104, 0};
        case Components::Lsm6dsoSamplingFrequency::SF_208Hz:
            return sensor_value{208, 0};
        case Components::Lsm6dsoSamplingFrequency::SF_416Hz:
            return sensor_value{416, 0};
        case Components::Lsm6dsoSamplingFrequency::SF_833Hz:
            return sensor_value{833, 0};
        case Components::Lsm6dsoSamplingFrequency::SF_1_66kHz:
            return sensor_value{1666, 0};
        case Components::Lsm6dsoSamplingFrequency::SF_3_33kHz:
            return sensor_value{3333, 0};
        case Components::Lsm6dsoSamplingFrequency::SF_6_66kHz:
            return sensor_value{6666, 0};
    }

    FW_ASSERT(0, static_cast<I32>(freqParam));

    return sensor_value{0, 0};
}

struct sensor_value ImuManager::getMagnetometerSamplingFrequency() {
    Fw::ParamValid valid;
    Components::Lis2mdlSamplingFrequency freqParam = this->paramGet_MAGNETOMETER_SAMPLING_FREQUENCY(valid);

    this->tlmWrite_MagnetometerSamplingFrequency(freqParam);

    switch (freqParam) {
        case Components::Lis2mdlSamplingFrequency::SF_10Hz:
            return sensor_value{10, 0};
        case Components::Lis2mdlSamplingFrequency::SF_20Hz:
            return sensor_value{20, 0};
        case Components::Lis2mdlSamplingFrequency::SF_50Hz:
            return sensor_value{50, 0};
        case Components::Lis2mdlSamplingFrequency::SF_100Hz:
            return sensor_value{100, 0};
    }

    FW_ASSERT(0, static_cast<I32>(freqParam));

    return sensor_value{0, 0};
}

bool ImuManager ::sensorValuesEqual(struct sensor_value* sv1, struct sensor_value* sv2) {
    return (sv1->val1 == sv2->val1) && (sv1->val2 == sv2->val2);
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void ImuManager ::GET_ACCELERATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    Fw::Success condition;
    Drv::Acceleration acceleration = this->accelerationGet_handler(0, condition);

    this->log_ACTIVITY_LO_AccelerationData(acceleration.get_x(), acceleration.get_y(), acceleration.get_z());

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ImuManager ::GET_ANGULAR_VELOCITY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    Fw::Success condition;
    Drv::AngularVelocity angularVelocity = this->angularVelocityGet_handler(0, condition);

    this->log_ACTIVITY_LO_AngularVelocityData(angularVelocity.get_x(), angularVelocity.get_y(),
                                              angularVelocity.get_z());

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ImuManager ::GET_MAGNETIC_FIELD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    Fw::Success condition;
    Drv::MagneticField magneticField = this->magneticFieldGet_handler(0, condition);

    this->log_ACTIVITY_LO_MagneticFieldData(magneticField.get_x(), magneticField.get_y(), magneticField.get_z());

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ImuManager ::GET_DIE_TEMPERATURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    F32 temperatureC = 0.0f;

    if (!this->dieTemperatureGet(temperatureC)) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    this->tlmWrite_DieTempC(temperatureC);
    this->log_ACTIVITY_LO_DieTemperatureData(temperatureC);

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components

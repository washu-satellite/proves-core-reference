// ======================================================================
// \title  ImuManager.hpp
// \brief  hpp file for ImuManager component implementation class
// ======================================================================

#ifndef Components_ImuManager_HPP
#define Components_ImuManager_HPP

#include "PROVESFlightControllerReference/Components/ImuManager/ImuManagerComponentAc.hpp"
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

namespace Components {

class ImuManager final : public ImuManagerComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct ImuManager object
    ImuManager(const char* const compName);

    //! Destroy ImuManager object
    ~ImuManager();

  public:
    // ----------------------------------------------------------------------
    // Helper methods
    // ----------------------------------------------------------------------

    //! Configure the IMU devices
    void configure(const struct device* lis2mdl, const struct device* lsm6dso);

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for accelerationGet
    //!
    //! Port to read the current acceleration in m/s^2.
    Drv::Acceleration accelerationGet_handler(FwIndexType portNum,  //!< The port number
                                              Fw::Success& condition) override;

    //! Handler implementation for angularVelocityGet
    //!
    //! Port to read the current angular velocity in rad/s.
    Drv::AngularVelocity angularVelocityGet_handler(FwIndexType portNum,  //!< The port number
                                                    Fw::Success& condition) override;

    //! Handler implementation for angularVelocityMagnitudeGet
    //!
    //! Formula for magnitude:
    //! |ω| = sqrt(ωx² + ωy² + ωz²)
    //! ωx, ωy, ωz are the angular velocity components in rad/s
    //!
    //! Formula for conversion to degrees per second:
    //! |ω|_deg/s = |ω|_rad/s * (180 / π)
    //!
    //! Port to read the current angular velocity magnitude.
    F64 angularVelocityMagnitudeGet_handler(FwIndexType portNum,  //!< The port number
                                            Fw::Success& condition,
                                            const Components::AngularUnit& unit) override;

    //! Handler implementation for magneticFieldGet
    //!
    //! Port to read the current magnetic field in gauss.
    Drv::MagneticField magneticFieldGet_handler(FwIndexType portNum,  //!< The port number
                                                Fw::Success& condition) override;

    //! Handler implementation for magneticFieldPeriodGet
    //!
    //! Port to get the time between magnetic field reads
    Fw::TimeIntervalValue magneticFieldSamplingPeriodGet_handler(FwIndexType portNum,  //!< The port number
                                                                 Fw::Success& condition) override;

    //! Handler implementation for run
    //!
    //! Port to trigger periodic data fetching and telemetry updating
    void run_handler(FwIndexType portNum,  //!< The port number
                     U32 context           //!< The call order
                     ) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for GET_ACCELERATION
    //!
    //! Command to get the current acceleration
    void GET_ACCELERATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    //! Handler implementation for GET_ANGULAR_VELOCITY
    //!
    //! Command to get the current angular velocity
    void GET_ANGULAR_VELOCITY_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    //! Handler implementation for GET_MAGNETIC_FIELD
    //!
    //! Command to get the current magnetic field
    void GET_MAGNETIC_FIELD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    //! Handler implementation for GET_DIE_TEMPERATURE
    //!
    //! Command to get the LSM6DSO on-die temperature
    void GET_DIE_TEMPERATURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

  private:
    // ----------------------------------------------------------------------
    //  Private helper methods
    // ----------------------------------------------------------------------
    //! Configure imu sensors
    void configureSensors(struct sensor_value& magn, struct sensor_value& accel, struct sensor_value& gyro);


    /**
     * \brief Fetch and read a sensor channel
     * 
     * \details
     * 
     * \param dev        Device to read
     * \param fetchChan  Channel to fetch (the XYZ aggregate)
     * \param chanX      X-axis channel 
     * \param chanY      Y-axis channel 
     * \param chanZ      Z-axis channel 
     * \param sensor     Identity used in the SensorReadFailed event
     * \param x          [out] X reading, untouched on failure
     * \param y          [out] Y reading, untouched on failure
     * \param z          [out] Z reading, untouched on failure
     * 
     * \return true when all four transactions succeed
     */
    bool readVector(const struct device* dev,
                    enum sensor_channel fetchChan,
                    enum sensor_channel chanX,
                    enum sensor_channel chanY,
                    enum sensor_channel chanZ,
                    Components::ImuSensor::T sensor,
                    struct sensor_value& x,
                    struct sensor_value& y,
                    struct sensor_value& z);

    /**
     * \brief Read the LSM6DSO on-die temperature
     * 
     * \details Disables after first read if temp is unavailable(ENOTSUP).
     * 
     * \param temperatureC  [out] Temperature in degrees C, zero on failure
     * \return true when a fresh reading was obtained
     */
    bool getDieTemperature(F32& temperatureC);

    //! Apply axis orientation parameter to sensor readings
    void applyAxisOrientation(struct sensor_value& x_val, struct sensor_value& y_val, struct sensor_value& z_val);

    //! Get accelerometer sampling frequency from parameter
    struct sensor_value getAccelerometerSamplingFrequency();

    //! Get gyroscope sampling frequency from parameter
    struct sensor_value getGyroscopeSamplingFrequency();

    //! Get LSM6DSO sampling frequency from parameter
    struct sensor_value getLsm6dsoSamplingFrequency(Components::Lsm6dsoSamplingFrequency freqParam);

    //! Get magnetometer sampling frequency from parameter
    struct sensor_value getMagnetometerSamplingFrequency();

    //! Compare two sensor_value structs for equality
    bool sensorValuesEqual(struct sensor_value* sv1, struct sensor_value* sv2);

    F64 convertAngularVelocityToUnit(Drv::AngularVelocity angularVelocity);

  private:
    // ----------------------------------------------------------------------
    // Private member variables
    // ----------------------------------------------------------------------

    //! Zephyr device storing the initialized LIS2MDL sensor
    const struct device* m_lis2mdl = nullptr;

    //! Zephyr device storing the initialized LSM6DSO sensor
    const struct device* m_lsm6dso = nullptr;

    //! Current odr values for sensors
    struct sensor_value m_curr_magn_odr = {0, 0};
    struct sensor_value m_curr_gyro_odr = {0, 0};
    struct sensor_value m_curr_accel_odr = {0, 0};

    //! False once the die-temperature channel reports ENOTSUP.
    bool m_dieTempSupported = true;
};

}  // namespace Components

#endif

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, climate, sensor, text_sensor, uart
from esphome.const import (
    DEVICE_CLASS_DOOR,
    DEVICE_CLASS_LOCK,
    DEVICE_CLASS_TEMPERATURE,
    ICON_THERMOMETER,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

CODEOWNERS = []
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]

CONF_CHILD_LOCK = "child_lock"
CONF_FILTER_DOOR = "filter_door"
CONF_COIL_TEMPERATURE = "coil_temperature"
CONF_AIR_QUALITY_RAW = "air_quality_raw"
CONF_AIR_QUALITY = "air_quality"
CONF_FILTER_STATUS = "filter_status"

ac_niu_ns = cg.esphome_ns.namespace("ac_niu")
AcNiuClimate = ac_niu_ns.class_(
    "AcNiuClimate", climate.Climate, cg.Component, uart.UARTDevice
)

CONFIG_SCHEMA = cv.All(
    climate.climate_schema(AcNiuClimate)
    .extend(
        {
            cv.Optional(CONF_CHILD_LOCK): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_LOCK
            ),
            cv.Optional(CONF_FILTER_DOOR): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_DOOR
            ),
            cv.Optional(CONF_COIL_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_AIR_QUALITY_RAW): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=1,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_AIR_QUALITY): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_FILTER_STATUS): text_sensor.text_sensor_schema(),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    cv.only_with_arduino,
)

FINAL_VALIDATE_SCHEMA = cv.All(
    uart.final_validate_device_schema(
        "ac_niu_climate",
        require_rx=True,
        require_tx=True,
        baud_rate=9600,
        data_bits=8,
        parity="EVEN",
        stop_bits=1,
    )
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_CHILD_LOCK in config:
        entity = await binary_sensor.new_binary_sensor(config[CONF_CHILD_LOCK])
        cg.add(var.set_child_lock_sensor(entity))
    if CONF_FILTER_DOOR in config:
        entity = await binary_sensor.new_binary_sensor(config[CONF_FILTER_DOOR])
        cg.add(var.set_filter_door_sensor(entity))
    if CONF_COIL_TEMPERATURE in config:
        entity = await sensor.new_sensor(config[CONF_COIL_TEMPERATURE])
        cg.add(var.set_coil_temperature_sensor(entity))
    if CONF_AIR_QUALITY_RAW in config:
        entity = await sensor.new_sensor(config[CONF_AIR_QUALITY_RAW])
        cg.add(var.set_air_quality_raw_sensor(entity))
    if CONF_AIR_QUALITY in config:
        entity = await text_sensor.new_text_sensor(config[CONF_AIR_QUALITY])
        cg.add(var.set_air_quality_sensor(entity))
    if CONF_FILTER_STATUS in config:
        entity = await text_sensor.new_text_sensor(config[CONF_FILTER_STATUS])
        cg.add(var.set_filter_status_sensor(entity))

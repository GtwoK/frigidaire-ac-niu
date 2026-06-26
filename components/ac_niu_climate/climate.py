import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, climate, sensor, switch, text_sensor, uart
from esphome.const import (
    DEVICE_CLASS_LOCK,
    DEVICE_CLASS_TEMPERATURE,
    ICON_THERMOMETER,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)

CODEOWNERS = []
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "sensor", "switch", "text_sensor"]

CONF_CHILD_LOCK = "child_lock"
CONF_PUREAIR_FILTER_INSTALLED = "pureair_filter_installed"
CONF_COIL_TEMPERATURE = "coil_temperature"
CONF_AIR_QUALITY_RAW = "air_quality_raw"
CONF_AIR_QUALITY = "air_quality"
CONF_FILTER_STATUS = "filter_status"
CONF_SLEEP_MODE = "sleep_mode"

ac_niu_ns = cg.esphome_ns.namespace("ac_niu")
AcNiuClimate = ac_niu_ns.class_(
    "AcNiuClimate", climate.Climate, cg.Component, uart.UARTDevice
)
AcNiuSleepSwitch = ac_niu_ns.class_("AcNiuSleepSwitch", switch.Switch)

CONFIG_SCHEMA = cv.All(
    climate.climate_schema(AcNiuClimate)
    .extend(
        {
            cv.Optional(CONF_CHILD_LOCK): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_LOCK
            ),
            cv.Optional(CONF_PUREAIR_FILTER_INSTALLED): binary_sensor.binary_sensor_schema(),
            cv.Optional(CONF_COIL_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_AIR_QUALITY_RAW): sensor.sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_AIR_QUALITY): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_FILTER_STATUS): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_SLEEP_MODE): switch.switch_schema(AcNiuSleepSwitch),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
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
    if CONF_PUREAIR_FILTER_INSTALLED in config:
        entity = await binary_sensor.new_binary_sensor(config[CONF_PUREAIR_FILTER_INSTALLED])
        cg.add(var.set_pureair_filter_installed_sensor(entity))
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
    if CONF_SLEEP_MODE in config:
        entity = await switch.new_switch(config[CONF_SLEEP_MODE])
        cg.add(entity.set_parent(var))
        cg.add(var.set_sleep_switch(entity))

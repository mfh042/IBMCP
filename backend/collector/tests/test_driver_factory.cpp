#include <gtest/gtest.h>

#include "drivers/driver_factory.h"
#include "drivers/modbus_address.h"

using namespace ibmcp;

TEST(NormalizeProtocol, StripsSeparatorsAndLowercases) {
    EXPECT_EQ(normalize_protocol("MODBUS_TCP"), "modbustcp");
    EXPECT_EQ(normalize_protocol("Modbus TCP"), "modbustcp");
    EXPECT_EQ(normalize_protocol("modbus-tcp"), "modbustcp");
    EXPECT_EQ(normalize_protocol("modbus.tcp"), "modbustcp");
    EXPECT_EQ(normalize_protocol("OPC_UA"), "opcua");
    EXPECT_EQ(normalize_protocol("MQTT"), "mqtt");
    EXPECT_EQ(normalize_protocol("  Socket-Udp. "), "socketudp");
    EXPECT_EQ(normalize_protocol(""), "");
}

TEST(CreateDriver, ModbusTcpResolvesViaNormalizedName) {
    // 无论是否有 libmodbus，工厂都返回实例（无库时为退化实例，connect 返回 false）。
    auto d1 = create_driver("modbus_tcp");
    ASSERT_NE(d1, nullptr);
    EXPECT_EQ(d1->protocol(), "modbustcp");

    auto d2 = create_driver("Modbus TCP");
    ASSERT_NE(d2, nullptr);
    EXPECT_EQ(d2->protocol(), "modbustcp");

    EXPECT_EQ(create_driver("not_a_protocol"), nullptr);
    EXPECT_EQ(create_driver(""), nullptr);
}

TEST(ParseModbusAddress, HoldingRegisterPrefix) {
    auto a = parse_modbus_address("4x0001");
    EXPECT_TRUE(a.valid);
    EXPECT_EQ(a.function_code, 3);
    EXPECT_EQ(a.register_index, 0); // 1-based -> 0-based

    auto b = parse_modbus_address("4x0003");
    EXPECT_TRUE(b.valid);
    EXPECT_EQ(b.function_code, 3);
    EXPECT_EQ(b.register_index, 2);

    auto c = parse_modbus_address("4x1001");
    EXPECT_TRUE(c.valid);
    EXPECT_EQ(c.register_index, 1000);
}

TEST(ParseModbusAddress, OtherAreas) {
    EXPECT_EQ(parse_modbus_address("0x0001").function_code, 1);
    EXPECT_EQ(parse_modbus_address("1x0001").function_code, 2);
    EXPECT_EQ(parse_modbus_address("3x0001").function_code, 4);
    EXPECT_EQ(parse_modbus_address("3x0001").register_index, 0);
}

TEST(ParseModbusAddress, BareNumberDefaultsToHolding) {
    auto a = parse_modbus_address("1001");
    EXPECT_TRUE(a.valid);
    EXPECT_EQ(a.function_code, 3);
    EXPECT_EQ(a.register_index, 1000);
}

TEST(ParseModbusAddress, InvalidInputs) {
    EXPECT_FALSE(parse_modbus_address("").valid);
    EXPECT_FALSE(parse_modbus_address("4x0").valid);      // offset 必须 >= 1
    EXPECT_FALSE(parse_modbus_address("4x-1").valid);
    EXPECT_FALSE(parse_modbus_address("garbage").valid);
    EXPECT_FALSE(parse_modbus_address("9x0001").valid);   // area 不在 0/1/3/4
}

TEST(ModbusRegisterCount, ByDataType) {
    EXPECT_EQ(modbus_register_count("float"), 2);
    EXPECT_EQ(modbus_register_count("double"), 2);
    EXPECT_EQ(modbus_register_count("int"), 1);
    EXPECT_EQ(modbus_register_count("bool"), 1);
    EXPECT_EQ(modbus_register_count(""), 1);
}

TEST(ModbusTcpDriver, DegradedWhenNoConnection) {
    // 不依赖真实设备/连接：未 connect 时 read_batch 全部 Bad，不崩溃。
    auto drv = create_driver("modbus_tcp");
    ASSERT_NE(drv, nullptr);
    std::vector<Point> pts;
    Point p;
    p.point_id = 1;
    p.address = "4x0001";
    p.data_type = "float";
    pts.push_back(p);
    auto res = drv->read_batch(pts);
    ASSERT_EQ(res.size(), 1u);
    EXPECT_EQ(res[0].point_id, 1);
    EXPECT_EQ(res[0].quality, "Bad");
    EXPECT_FALSE(res[0].error.empty());
    EXPECT_FALSE(drv->write(p, "1.0")); // 未连接写失败
}
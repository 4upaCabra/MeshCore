#include <gtest/gtest.h>

#include <helpers/BatteryPack.h>

class BatteryPackTest : public ::testing::Test {
protected:
  void SetUp() override {
    BatteryPack::clearPackMilliVolts();
    BatteryPack::setClockMs(nullptr);
    BatteryPack::setClockMsValue(0);
  }
};

TEST_F(BatteryPackTest, LithiumFullAndEmpty) {
  EXPECT_EQ(BatteryPack::getPercent(4200, BatteryChemistry::Lithium), 100);
  EXPECT_EQ(BatteryPack::getPercent(4300, BatteryChemistry::Lithium), 100);
  EXPECT_EQ(BatteryPack::getPercent(3000, BatteryChemistry::Lithium), 0);
  EXPECT_EQ(BatteryPack::getPercent(2500, BatteryChemistry::Lithium), 0);
}

TEST_F(BatteryPackTest, LithiumPlateauBelowLinear) {
  // Linear 3.0–4.2 V would map 3700 → ~58%. OCV plateau is much lower.
  const int linear =
      ((3700 - 3000) * 100) / (4200 - 3000);
  const int ocv = BatteryPack::getPercent(3700, BatteryChemistry::Lithium);
  EXPECT_EQ(linear, 58);
  EXPECT_EQ(ocv, 25);
  EXPECT_LT(ocv, linear - 20);
}

TEST_F(BatteryPackTest, LithiumInterpolatesBetweenPoints) {
  // Midway 4100 (90) and 4000 (80) → 4050 ≈ 85.
  EXPECT_EQ(BatteryPack::getPercent(4050, BatteryChemistry::Lithium), 85);
}

TEST_F(BatteryPackTest, LiFePO4PlateauDiffersFromLithium) {
  const int lipo = BatteryPack::getPercent(3300, BatteryChemistry::Lithium);
  const int lfp = BatteryPack::getPercent(3300, BatteryChemistry::LiFePO4);
  EXPECT_EQ(lipo, 2);
  EXPECT_EQ(lfp, 40);
  EXPECT_NE(lipo, lfp);
}

TEST_F(BatteryPackTest, NiMHAndLeadAcidSmoke) {
  EXPECT_EQ(BatteryPack::getPercent(1400, BatteryChemistry::NiMH), 100);
  EXPECT_EQ(BatteryPack::getPercent(1000, BatteryChemistry::NiMH), 0);
  EXPECT_GT(BatteryPack::getPercent(1250, BatteryChemistry::NiMH), 0);
  EXPECT_LT(BatteryPack::getPercent(1250, BatteryChemistry::NiMH), 100);

  EXPECT_EQ(BatteryPack::getPercent(2150, BatteryChemistry::LeadAcid), 100);
  EXPECT_EQ(BatteryPack::getPercent(1800, BatteryChemistry::LeadAcid), 0);
  EXPECT_GT(BatteryPack::getPercent(2000, BatteryChemistry::LeadAcid), 0);
}

TEST_F(BatteryPackTest, SeriesCellsTwoS) {
  // 2S pack at 7.4 V ≡ 3.7 V/cell on Lithium curve.
  EXPECT_EQ(BatteryPack::getPercent(7400, BatteryChemistry::Lithium, 2),
            BatteryPack::getPercent(3700, BatteryChemistry::Lithium, 1));
}

TEST_F(BatteryPackTest, CustomFallsBackToLithium) {
  EXPECT_EQ(BatteryPack::getPercent(4200, BatteryChemistry::Custom), 100);
  EXPECT_EQ(BatteryPack::getPercent(3700, BatteryChemistry::Custom),
            BatteryPack::getPercent(3700, BatteryChemistry::Lithium));
}

TEST_F(BatteryPackTest, DefaultActiveProfileIsLithium) {
  EXPECT_EQ(BatteryPack::activeProfile().chem, BatteryChemistry::Lithium);
  EXPECT_EQ(BatteryPack::getPercent(4200), 100);
}

TEST_F(BatteryPackTest, InaCacheFreshAndTtl) {
  uint16_t mv = 0;
  EXPECT_FALSE(BatteryPack::hasFreshPackMilliVolts(mv));

  BatteryPack::setClockMsValue(1000);
  BatteryPack::notePackMilliVolts(3700);
  ASSERT_TRUE(BatteryPack::hasFreshPackMilliVolts(mv));
  EXPECT_EQ(mv, 3700);

  BatteryPack::setClockMsValue(1000 + BATT_PACK_INA_TTL_MS);
  EXPECT_TRUE(BatteryPack::hasFreshPackMilliVolts(mv));

  BatteryPack::setClockMsValue(1000 + BATT_PACK_INA_TTL_MS + 1);
  EXPECT_FALSE(BatteryPack::hasFreshPackMilliVolts(mv));
}

TEST_F(BatteryPackTest, InaCacheRejectsGarbage) {
  uint16_t mv = 0;
  BatteryPack::setClockMsValue(1);
  BatteryPack::notePackMilliVolts(100);  // too low
  EXPECT_FALSE(BatteryPack::hasFreshPackMilliVolts(mv));
  BatteryPack::notePackMilliVolts(25000);  // too high
  EXPECT_FALSE(BatteryPack::hasFreshPackMilliVolts(mv));
}

// The native environment's googletest package ships no gtest_main, so every
// public-line test file provides its own entry point.
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

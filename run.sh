cd obj/gateway_front_tier/
gnome-terminal -e "./gateway_front_tier ../../SampleConfigurationFiles/SampleGatewayConfigurationFile1.txt ../../output/SampleGatewayFrontTierOutputFile1.txt"
gnome-terminal -e "./gateway_front_tier ../../SampleConfigurationFiles/SampleGatewayConfigurationFile2.txt ../../output/SampleGatewayFrontTierOutputFile2.txt"
cd ../gateway_back_tier/
gnome-terminal -e "./gateway_back_tier ../../SampleConfigurationFiles/SampleGatewayConfigurationFile1.txt ../../output/SamplePersistentStorageFile1.txt"
gnome-terminal -e "./gateway_back_tier ../../SampleConfigurationFiles/SampleGatewayConfigurationFile2.txt ../../output/SamplePersistentStorageFile2.txt"
cd ../door_sensor/
gnome-terminal -e "./door_sensor ../../SampleConfigurationFiles/SampleSensorConfigurationFile2.txt ../../SampleConfigurationFiles/SampleDoorSensorInputFile.txt ../../output/SampleDoorSensorOutputFile.txt"
cd ../motion_sensor/
gnome-terminal -e "./motion_sensor ../../SampleConfigurationFiles/SampleSensorConfigurationFile1.txt ../../SampleConfigurationFiles/SampleSensorInputFile1.txt ../../output/SampleMotionSensorOutputFile.txt"
cd ../key_chain_sensor/
gnome-terminal -e "./key_chain_sensor ../../SampleConfigurationFiles/SampleSensorConfigurationFile3.txt ../../SampleConfigurationFiles/SampleSensorInputFile2.txt ../../output/SampleKeyChainSensorOutputFile.txt"
cd ../device/
gnome-terminal -e "./device ../../SampleConfigurationFiles/SampleSmartDeviceConfigurationFile1.txt ../../output/SampleDeviceOutput.txt"
cd ../../

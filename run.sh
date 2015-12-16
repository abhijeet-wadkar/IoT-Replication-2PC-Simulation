cd obj/gateway_front_tier/
gnome-terminal -e "./gateway_front_tier ../../ConfigFiles/PrimaryGatewayConfig.txt ../../output/PrimaryGateway.log"
gnome-terminal -e "./gateway_front_tier ../../ConfigFiles/SecondaryGatewayConfig.txt ../../output/SecondaryGateway.log"
cd ../gateway_back_tier/
gnome-terminal -e "./gateway_back_tier ../../ConfigFiles/PrimaryBackendConfig.txt ../../output/PrimaryBackend.log"
gnome-terminal -e "./gateway_back_tier ../../ConfigFiles/SecondaryBackendConfig.txt ../../output/SecondaryBackend.log"
cd ../door_sensor/
gnome-terminal -e "./door_sensor  ../../ConfigFiles/DoorConfig.txt ../../InputFiles/DoorInput.txt ../../output/Door.log"
cd ../motion_sensor/
gnome-terminal -e "./motion_sensor ../../ConfigFiles/MotionConfig.txt ../../InputFiles/MotionInput.txt ../../output/Motion.log"
cd ../key_chain_sensor/
gnome-terminal -e "./key_chain_sensor ../../ConfigFiles/KeychainConfig.txt ../../InputFiles/KeychainInput.txt ../../output/Keychain.log"
cd ../device/
gnome-terminal -e "./device ../../ConfigFiles/SecurityDeviceConfig.txt ../../output/Device.log"
cd ../../

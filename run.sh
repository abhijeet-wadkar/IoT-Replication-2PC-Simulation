gnome-terminal -e "bash -c \"./output/gateway_front_tier ConfigFiles/PrimaryGatewayConfig.txt PrimaryGatewayOutput.log; exec bash\""
sleep 4
gnome-terminal -e "bash -c \"./output/gateway_front_tier ConfigFiles/SecondaryGatewayConfig.txt SecondaryGatewayOutput.log; exec bash\""
sleep 4
gnome-terminal -e "bash -c \"./output/gateway_back_tier ConfigFiles/PrimaryBackendConfig.txt PrimaryBackendOutput.log; exec bash\""
sleep 4
gnome-terminal -e "bash -c \"./output/gateway_back_tier ConfigFiles/SecondaryBackendConfig.txt SecondaryBackendOutput.log; exec bash\""
sleep 4
gnome-terminal -e "bash -c \"./output/motion_sensor ConfigFiles/MotionConfig.txt InputFiles/MotionInput.txt MotionOutput.log; exec bash\""
sleep 4
gnome-terminal -e "bash -c \"./output/device ConfigFiles/SecurityDeviceConfig.txt SecurityDeviceOutput.log; exec bash\""
sleep 4
gnome-terminal -e "bash -c \"./output/door_sensor ConfigFiles/DoorConfig.txt InputFiles/DoorInput.txt DoorOutput.log; exec bash\""
sleep 4
gnome-terminal -e "bash -c \"./output/key_chain_sensor ConfigFiles/KeychainConfig.txt InputFiles/KeychainInput.txt KeychainOutput.log; exec bash\""

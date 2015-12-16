Assumptions:
##Person entering the house inferences
1) door_open -> motion_detected -> key_detected 
	or
   door_open -> key_detected -> motion_detected
	
	We are considering above series of events as user entered home. In above cases if key is not detected then the entered person is an intruder.

## Person exiting the house inferences
2) motion_detected -> door_closed
	We are considering above series of events as a person exited the house.

## Unwanted motion in the house
3) motion_detected -> key_chain_not_detected
	Here, irrespective of door being close or open, security alert has been raised

## Other inferences
4) Other inference rules are tried to be kept in accordance with above two inferences.


#################Fault Tolerance######################
In our project, we have handled both the primary and secondary crashes.

In this project, we have used two buffers in the gateway viz. unordered_msg_buffer (message before ordering according the verctor clock) and ordered_msg_buffer (message after logically ordering which are to be sent to the backend). If any of the gateways fails and any of the buffer has unprocesed messages, there will be message loss - all the messages which are there in the queue at the time crash wil be lost.

If any of the process (sensors or security device) fail to send/receive messages to/from the gateway (explicitly, we check for the broken pipe error or SIGPIPE errorno) , we try to connect to replica/second gateway (handled for both the primary and seconday crashes).

Handling vectors clock after crash: new gateway will adjust its clock based on the first messages from each of the sensors so that it can logically order rest of the messages and also infer the user context.
##############end######################


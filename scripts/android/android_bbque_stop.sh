PROCESS_NUM=$(ps -a | grep 'barbeque' | awk '{print $2}' | wc -l)
if [ $PROCESS_NUM -eq 1 ];
then
	echo "Stopping BarbequeRTRM gracefully..."
	kill -SIGINT $(ps -a | grep 'barbeque' | awk '{print $2}') 
else
	echo "BarbequeRTRM is not running"
	echo "To start the BarbequeRTRM launch android_bbque_start.sh script"
fi

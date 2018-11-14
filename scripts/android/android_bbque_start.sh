PROCESS_NUM=$(ps -a | grep 'barbeque' | awk '{print $2}' | wc -l)
if [ $PROCESS_NUM -eq 1 ];
then
	echo "BarbequeRTRM is already running"
	echo "To stop the BarbequeRTRM launch the android_bbque_stop.sh script"
else
	echo "Exporting LD library path..."

	export LD_LIBRARY_PATH=/data/bosp/lib/bbque

	echo "De-enforcing SELinux policies..."

	setenforce 0

	echo "Launching BarbequeRTRM..."
	./sbin/barbeque &
fi
#sleep 2

#echo "Changing FIFO permission..."
#chmod 777 var/rpc_fifo

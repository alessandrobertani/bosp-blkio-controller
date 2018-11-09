echo "Exporting LD library path..."

export LD_LIBRARY_PATH=/data/bosp/lib/bbque

echo "De-enforcing SELinux policies..."

setenforce 0

echo "Launching BarbequeRTRM..."
./sbin/barbeque &

#sleep 2

#echo "Changing FIFO permission..."
#chmod 777 var/rpc_fifo

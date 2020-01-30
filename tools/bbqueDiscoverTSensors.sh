#!/bin/bash

cmake_outfile=BbquePMConfig.cmake
prefix_dir=${1:-/sys/devices/platform}
hw_pattern=*hwmon[0-9]
nr_sockets=0
tsensors=""

while read line
do
	(( nr_sockets += 1 ))
	[[ $tsensors != "" ]] && tsensors="$tsensors;"
	tsensors="$tsensors$line"
done <<<$(find $prefix_dir -name $hw_pattern)

echo "Nr. CPU sockets: " $nr_sockets
echo "Thermal sensors: " $tsensors

echo "set (CONFIG_BBQUE_PM_NR_CPU_SOCKETS $nr_sockets)" > $cmake_outfile
echo "set (CONFIG_BBQUE_PM_TSENSOR_PATHS $tsensors)"   >> $cmake_outfile


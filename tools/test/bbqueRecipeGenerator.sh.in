#!/bin/bash

RECIPE_NAME="output.recipe"
$ITERATIONS=10

function PrintUsage {

	echo
	echo "[ERROR] $1"
	echo
	echo "Usage: ./RecipeGenerator.sh arg-1 arg-2 arg-3 arg-4"
	echo "   arg-1: integer max CPU quota (e.g. 600: 6 CPUs)"
	echo "   arg-2: integer min CPU quota (e.g. 200: 2 CPUs)"
	echo "   arg-3: integer CPU quota step (e.g. 300: awm_n: X cores, awm_n+1: X-3 cores)"
	echo "   arg-4: app launch string (e.g. '/path/to/app/appname args')"

	exit

}

function CheckArguments {

	A_NUMBER='^[0-9]+$'
	APP_BIN=$(echo "$4" | awk -F' ' '{print $1}')

	[[ $1 =~ $A_NUMBER ]] || PrintUsage "$1 is not a number"
	[[ $2 =~ $A_NUMBER ]] || PrintUsage "$2 is not a number"
	[[ $3 =~ $A_NUMBER ]] || PrintUsage "$3 is not a number"
	[ -f $APP_BIN ]       || PrintUsage "$4 is not a file"

}

function InsertHead {

	echo "<?xml version=\"1.0\"?>" > $1
	echo "<BarbequeRTRM recipe_version=\"0.8\">" >> $1
	echo "    <application priority=\"1\">" >> $1
	echo "        <platform id=\"org.linux.cgroup\">" >> $1
	echo "            <awms>" >> $1

}

function InsertTail {

	echo "            </awms>" >> $1
	echo "        </platform>" >> $1
	echo "    </application>" >> $1
	echo "</BarbequeRTRM>" >> $1
	echo "<!-- vim: set tabstop=4 filetype=xml : -->" >> $1

}

function InsertBody {

	echo "                <awm id=\"$2\" name=\"awm-$2\" value=\"@VALUE_$2@\">" >> $1
	echo "                    <resources>" >> $1
	echo "                        <sys id=\"0\">" >> $1
	echo "                            <cpu id=\"0\">" >> $1
	echo "                                <pe qty=\"$3\"/>" >> $1
	echo "                                <mem units=\"Mb\" qty=\"100\"/>" >> $1
	echo "                            </cpu>" >> $1
	echo "                        </sys>" >> $1
	echo "                    </resources>" >> $1
	echo "                </awm>" >> $1

}



################################################################################
# THE SCIPT
################################################################################

#-------------------------------------------------------------------------------
# Check Perf
HAVE_PERF=$(perf --version)
if [[ "$HAVE_PERF" != "perf version"* ]]; then
	echo "Please install perf tool"
	exit
fi

#-------------------------------------------------------------------------------
# Arg check
if [[ $# != 4 ]]; then
	PrintUsage "Invalid number of args"
fi

CheckArguments $1 $2 $3 $4

RESOURCES=$(seq $1 -$3 $2)
IDS=$(seq 0 $(echo "$RESOURCES" | \
	awk -F' ' '{wordcount++}END{print wordcount-1}'))

[ "$RESOURCES" != "" ] || PrintUsage "Cannot create AWMs with these args"

AWM_RES=($RESOURCES)
AWM_IDS=($IDS)

#-------------------------------------------------------------------------------
# Create recipe
InsertHead $RECIPE_NAME

for ID in "${AWM_IDS[@]}"; do

	RES=${AWM_RES[$ID]}
	InsertBody $RECIPE_NAME $ID $RES

done

InsertTail $RECIPE_NAME
#-------------------------------------------------------------------------------
# Perform tests
rm -f values-temp

echo "Please start Barbeque then hit Enter"
read -p ""

#for each awm
for RES in "${AWM_RES[@]}"; do

	echo "Testing AWM with RES: $RES"

	InsertHead temp-$RECIPE_NAME
	InsertBody temp-$RECIPE_NAME 0 $RES
	InsertTail temp-$RECIPE_NAME

	perf stat -o perf_tmp -r $ITERATIONS -n $4 -r temp-$RECIPE_NAME > /dev/null
	NEW_VALUE=$(tail -n2 perf_tmp | grep 'seconds' | awk -F' ' '{print $1}' | sed -e 's/,/./g')
	echo $NEW_VALUE >> values-temp
	rm perf_tmp temp-$RECIPE_NAME

done

echo
echo "If you want to stop Barbeque, do it now then hit Enter"
read -p ""

SUM=$(cat values-temp | awk '{count+=$1}END{print count}')
NORMALIZED_VALUES=$(cat values-temp | awk '{printf("%d\n", (100*$1)/'$SUM')}')
rm values-temp
#-------------------------------------------------------------------------------
# Fill Recipe
ID=0
for VALUE in $NORMALIZED_VALUES; do
	cat $RECIPE_NAME | sed -e 's/@VALUE_'$ID'@/'$VALUE'/g' > _$RECIPE_NAME
	cat _$RECIPE_NAME > $RECIPE_NAME
	rm _$RECIPE_NAME
	let ID++
done

exit

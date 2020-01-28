#!/bin/bash

BOSP_BASE=${1:-'/opt/BOSP'}
APP_NAME=${2:-'BbqApp'}
APP_PATH=${3:-${BOSP_BASE}/contrib/user/${APP_NAME}}

echo
echo '.:: BarbequeRTRM Template Application Extraction'
echo 'Application name : ' $APP_NAME
echo 'Destination path : ' $APP_PATH

usage() {
echo
echo 'Usage:' $0 '<BOSP_BASE> <APP_NAME>'
echo 'Where:'
echo ' <BOSP_BASE> - path to BOSP installation'
echo ' <APP_NAME>  - the name of the new contrib application to initialize'
echo
}

if [ ! -d $BOSP_BASE ]; then
	echo
	echo 'ERROR: BOSP base folder ['$BOSP_BASE'] not found'
	usage
	exit 1
fi

if [ -d $APP_PATH ]; then
	echo
	echo 'ERROR: Application path ['$APP_PATH'] already existing'
	echo '  Move the previous application or change application name'
	usage
	exit 1
fi

mkdir -p $APP_PATH

ARCHIVE=`awk '/^__ARCHIVE_BELOW__/ {print NR + 1; exit 0; }' $0`
tail -n+$ARCHIVE $0 | tar xj -C $APP_PATH
if [ $! ]; then
	echo
	echo 'ERROR: Template extraction FAILED'
	exit 2
fi

CDIR=`pwd`
cd $APP_PATH

# Replace Template tokens according to application name
find . -type f | while read FILE; do
	echo 'Extracing ['$FILE']...'
	mv $FILE $FILE.org
	sed \
		-e "s/MYAPP/${APP_NAME^^}/g" \
		-e "s/MyApp/${APP_NAME^}/g"  \
		-e "s/myapp/${APP_NAME,,}/g" \
		$FILE.org > ${FILE/MyApp/$APP_NAME}
	rm $FILE.org
done

cd $CDIR
exit 0

__ARCHIVE_BELOW__

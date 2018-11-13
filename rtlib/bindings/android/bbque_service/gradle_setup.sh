echo $1
cp $1/local.properties.in $1/local.properties

echo $2
echo "sdk.dir=$2/ndk-bundle" >> $1/local.properties
echo "sdk.dir=$2" >> $1/local.properties


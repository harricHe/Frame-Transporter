#!/bin/bash -e

function finish
{
	JOBS=$(jobs -rp)
	if [[ !  -z  ${JOBS}  ]] ; then
		echo ${JOBS} | xargs kill ;
	fi
	rm -f /tmp/sc-test-forward-01.bin /tmp/sc-test-forward-01o.bin /tmp/sc-test-forward-01.chsum
}
trap finish EXIT

./forward 127.0.0.1 12346 &
nc -d -l 127.0.0.1 12346 > /tmp/sc-test-forward-01o.bin &
NC_PID=$!

dd if=/dev/urandom count=10240 bs=1024 > /tmp/sc-test-forward-01.bin

cat /tmp/sc-test-forward-01.bin | pv -r | nc 127.0.0.1 12345

wait ${NC_PID}

ls -l  /tmp/sc-test-forward-01.bin /tmp/sc-test-forward-01o.bin
shasum /tmp/sc-test-forward-01.bin /tmp/sc-test-forward-01o.bin

cat /tmp/sc-test-forward-01.bin | shasum  > /tmp/sc-test-forward-01.chsum
cat /tmp/sc-test-forward-01o.bin | shasum --check /tmp/sc-test-forward-01.chsum

echo "TEST $0 OK"

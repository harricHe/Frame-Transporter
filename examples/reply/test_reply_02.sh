#!/bin/bash -e

function finish
{
	JOBS=$(jobs -rp)
	[[ !  -z  ${JOBS}  ]] && ( echo ${JOBS} | xargs -n1 kill )
}
trap finish EXIT

./reply &
TASK_PID=$!
sleep 0.2

dd if=/dev/zero count=1024000 bs=1024| nc 127.0.0.1 12345  | pv -r > /dev/null

kill ${TASK_PID}
wait

echo "TEST $0 OK"

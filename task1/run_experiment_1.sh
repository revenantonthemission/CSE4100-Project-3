#!/bin/bash

# 서버 실행 명령 (포트번호는 상황에 맞게 수정)
PORT=65525
SERVER_EXEC="./stockserver $PORT"

# 클라이언트 실행 명령 (IP는 localhost 또는 cspro IP)
HOST="163.239.88.120"
CLIENT_EXEC="./multiclient $HOST $PORT"

# 측정할 클라이언트 수 목록
CLIENT_COUNTS=(10 20 30 40 50 60 70 80 90 100)

# 결과 파일
RESULT_FILE="result.txt"
echo "ClientCount ElapsedTime" > $RESULT_FILE

for COUNT in "${CLIENT_COUNTS[@]}"
do
    echo "▶ Running with $COUNT clients..."

    # multiclient 내부에서 gettimeofday() 출력하도록 구현되어 있어야 함
    OUTPUT=$(CLIENTS=$COUNT ./multiclient $HOST $PORT $COUNT | grep "Total Elapsed Time")
    echo "$OUTPUT"

    # 결과 정리
    ELAPSED=$(echo "$OUTPUT" | awk '{print $4}')
    echo "$COUNT $ELAPSED" >> $RESULT_FILE

    echo "▶ Killing server..."
    kill $SERVER_PID
    sleep 1
done

echo "✅ 실험 완료! 결과는 $RESULT_FILE 에 저장됨."
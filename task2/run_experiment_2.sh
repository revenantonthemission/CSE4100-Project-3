#!/bin/bash

# 설정
PORT=60125
HOST="163.239.14.107"
CLIENTS=20        # 고정된 클라이언트 수
LOOP=50           # 각 클라이언트당 요청 수
SERVER_EXEC="./stockserver $PORT"
MULTICLIENT="./multiclient"

# 비율 조합 리스트 (SHOW BUY SELL)
RATIO_LIST=(
    "50 0 0"
    "40 5 5"
    "30 10 10"
    "20 15 15"
    "10 20 20"
    "0 25 25"
    "0 50 0"
    "5 40 5"
    "10 30 10"
    "15 20 15"
    "20 10 20"
    "25 0 25"
    "0 0 50"
    "5 5 40"
    "10 10 30"
    "15 15 20"
    "20 20 10"
    "25 25 0"
)

RESULT_FILE="request_ratio_results.txt"
echo "ShowRatio BuyRatio SellRatio ElapsedTime" > $RESULT_FILE

# 컴파일
echo "🔧 Building..."
make clean && make
if [ $? -ne 0 ]; then
    echo "❌ Build failed. Check your Makefile or source code."
    exit 1
fi

# 실험 루프
for ratio in "${RATIO_LIST[@]}"
do
    read SHOW BUY SELL <<< "$ratio"

    echo -e "\n▶ Starting stockserver for ratio SHOW:$SHOW BUY:$BUY SELL:$SELL..."
    $SERVER_EXEC &
    SERVER_PID=$!
    sleep 1

    echo "▶ Running multiclient..."
    OUTPUT=$($MULTICLIENT $HOST $PORT $CLIENTS $LOOP $SHOW $BUY $SELL | grep "동시 처리율")
    echo "$OUTPUT"

    # 시간 추출 및 결과 저장
    ELAPSED=$(echo "$OUTPUT" | awk '{print $4}')
    echo "$SHOW $BUY $SELL $ELAPSED" >> $RESULT_FILE

    echo "▶ Killing stockserver..."
    kill $SERVER_PID
    sleep 1
done

echo -e "\n✅ 요청 비율 실험 완료! 결과는 $RESULT_FILE 에 저장됨."
#!/bin/bash

HOST=localhost
PORT=7072

while read -a data; do
  batt_txt="ok"
  batt_stat=""
  warnings="none"
  device=""
  temp=""
  humid=""

  case ${data[0]} in
    wt440h)
      device="${data[0]}_${data[1]}_${data[2]}"
      temp="${data[6]}"
      humid="${data[5]}"
      if [ ${data[4]} -eq 1 ]; then
        batt_txt="low"
        batt_stat=", Bat.low!"
        warnings="battery"
      fi
      if [ ${data[5]} -le 14 ]; then
        humid="0"
      fi
    ;;

    auriol)
      device="${data[0]}_${data[1]}"
      temp="${data[5]}"
      humid="${data[6]}"
      if [ ${data[2]} -eq 1 ]; then
        batt_txt="low"
        batt_stat=", Bat.low!"
        warnings="battery"
      fi
      if [ ${data[6]} -le 20 ]; then
        humid="0"
      fi
    ;;

    mebus)
      device="${data[0]}_${data[1]}"
      temp="${data[3]}"
      humid="${data[4]}"
    ;;

    ws1700)
      device="${data[0]}_${data[1]}_${data[2]}"
      temp="${data[5]}"
      humid="${data[6]}"
      if [ ${data[3]} -eq 0 ]; then
        batt_txt="low"
        batt_stat=", Bat.low!"
        warnings="battery"
      fi
    ;;

    rftech)
      device="${data[0]}_${data[1]}"
      temp="${data[3]}"
    ;;

  esac

  if [ -n "${device}" ]; then
    if [ -n "${humid}" ]; then
      echo -ne "setreading ${device} temperature ${temp}\nsetreading ${device} humidity ${humid}\nsetreading ${device} battery ${batt_txt}\nsetreading ${device} warnings ${warnings}\nset ${device} ${temp}°C, ${humid}%${batt_stat}\n" | nc ${HOST} ${PORT}
    else
      echo -ne "setreading ${device} temperature ${temp}\nsetreading ${device} battery ${batt_txt}\nsetreading ${device} warnings ${warnings}\nset ${device} ${temp}°C${batt_stat}\n" | nc ${HOST} ${PORT}
    fi
  fi
done

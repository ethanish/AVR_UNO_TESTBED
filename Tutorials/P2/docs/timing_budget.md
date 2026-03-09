# P2 Timing Budget and Jitter Notes

## Tick Configuration
- Timer: `Timer1` CTC mode
- Core clock: `16MHz`
- Prescaler: `64`
- Timer count period: `4us`
- Compare top: `OCR1A=249`
- Tick period: `1ms` (1000Hz)

## Task Rates
- `task_1ms`: every tick (fixed)
- `task_10ms`: default every 10 ticks (`SET RATE 10 <ms>`로 변경 가능)
- `task_100ms`: default every 100 ticks (`SET RATE 100 <ms>`로 변경 가능)

Expected ratio over 1000 ticks:
- `TASK1=1000`
- `TASK10=100`
- `TASK100=10`

Example (runtime changed):
- `SET RATE 10 20`, `SET RATE 100 250`
- Over 1000 ticks: `TASK10=50`, `TASK100=4`

## Jitter Metric in P2
This project logs ISR entry latency as jitter proxy:
- `latency_counts = TCNT1` at ISR entry
- `latency_us = latency_counts * 4`

Reported command:
- `GET JITTER`
- Returns: min/max/avg latency in microseconds + sample count

## Recommended Measurement Procedure
1. Flash `P2` firmware.
2. Open serial terminal at `115200`.
3. Observe counters with `GET TASKS`.
4. Measure latency stats with `GET JITTER`.
5. Run under different UART traffic loads and compare jitter spread.

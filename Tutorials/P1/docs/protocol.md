# P1 UART Protocol Spec

## Transport
- Interface: UART (8N1)
- Baud rate: 115200
- Line ending: `\n` (firmware accepts CR/LF)
- Encoding: ASCII

## Frame Format
- Request: `<COMMAND> [ARG1] [ARG2]\n`
- Response (success): `OK <payload>`
- Response (error): `ERR <code>`

## Commands
- `PING`
  - Response: `OK PONG`
- `HELP`
  - Response: `OK CMDS=PING,HELP,VERSION,SET,GET (GET: LED,PWM,ADC,STAT)`
- `VERSION`
  - Response: `OK VERSION=P1-1.0`
- `SET LED <0|1>`
  - Response: `OK LED=<0|1>`
- `GET LED`
  - Response: `OK LED=<0|1>`
- `SET PWM <0..255>`
  - Response: `OK PWM=<0..255>`
- `GET PWM`
  - Response: `OK PWM=<0..255>`
- `GET ADC <0..5>`
  - Response: `OK ADC=<0..1023>`
- `GET STAT`
  - Response: `OK IRQ=<count>`

## Error Codes
- `BAD_CMD`: unknown command
- `BAD_ARG`: missing or malformed argument
- `BAD_TARGET`: unknown GET/SET target
- `BAD_RANGE`: out-of-range numeric argument
- `LINE_TOO_LONG`: command length exceeds internal buffer
- `RX_OVERFLOW`: UART RX ring buffer overflow

## Timeout / Retry (Runner Side)
- Recommended read timeout: 700ms
- Retry policy: 1 immediate retry for transient serial timeout

## Integrity
This P1 version is line-based and does not include CRC/checksum yet.
CRC/checksum is planned for a later protocol revision.

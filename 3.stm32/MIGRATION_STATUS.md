| Module          | Source path                          | Source commit | Status        | Host test | STM32 build | HIL    | Parity    | Note |
| --------------- | ------------------------------------ | ------------- | ------------- | --------- | ----------- | ------ | --------- | ---- |
| I2C port        | `2.firmware/src/ports/i2c_port.h`    | `<sha>`       | `COPIED`      | `PASS`    | `PASS`      | `PASS` | `PASS`    |      |
| I2C manager     | `2.firmware/src/infrastructure/bus/` | `<sha>`       | `INTEGRATED`  | `PASS`    | `PASS`      | `PASS` | `PASS`    |      |
| F-RAM driver    | `2.firmware/src/drivers/storage/`    | `<sha>`       | `HW_VERIFIED` | `PASS`    | `PASS`      | `PASS` | `PENDING` |      |
| Storage service | `2.firmware/src/services/storage/`   | `<sha>`       | `NOT_STARTED` | `PASS`    | `—`         | `—`    | `—`       |      |
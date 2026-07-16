# ZSSC3241 — Register Notes

> **Loại tài liệu:** Ghi chú NVM, shadow register và bit-field  
> **Phạm vi:** Dùng chung cho các hệ thống sử dụng ZSSC3241  
> **Linh kiện:** Renesas ZSSC3241  
> **Trạng thái:** Bản nền tảng để thiết kế cấu hình và driver  
> **Nguồn chuẩn:** ZSSC3241 Datasheet, Rev. Feb. 2, 2024 — Table 35

---

## 1. Mục đích và nguyên tắc sử dụng

Tài liệu này là bản đồ kỹ thuật có chú giải cho NVM của ZSSC3241. Nó dùng để:

- thiết kế image cấu hình production;
- định nghĩa register/bit-field trong firmware;
- review ảnh hưởng của từng nhóm cấu hình;
- quản lý hệ số calibration 24 bit được chia qua nhiều word;
- tránh ghi nhầm vùng Renesas hoặc khóa NVM quá sớm.

Đây không phải bản thay thế datasheet. Mọi image NVM trước khi sản xuất phải được đối chiếu lại với **Table 35** của đúng revision silicon/datasheet.

## 2. Mô hình bộ nhớ

- NVM tổ chức theo word 16 bit.
- Customer-use NVM: `0x00–0x35`, tổng cộng 54 word.
- Renesas-use NVM: `0x36–0x3F`; customer có thể đọc nhưng không được ghi bằng command thông thường.
- Ghi customer NVM bằng opcode `0x40 + address`.
- Đọc NVM bằng opcode bằng chính địa chỉ `0x00–0x3F`.
- Một số word được nạp vào shadow registers sau reset.
- Shadow overwrite không làm hao chu kỳ NVM và mất hiệu lực sau POR/RESQ.
- Độ bền ghi NVM danh nghĩa khoảng 10.000 lần.

## 3. Bản đồ NVM tổng quát

| Địa chỉ | Tên/nhóm | Nội dung chính |
|---:|---|---|
| `0x00` | `Cust_ID0` | Customer ID word 0 |
| `0x01` | `Cust_ID1` | Customer ID word 1 |
| `0x02` | Interface configuration | I²C/OWI address, EOC, SPI, cyclic period, curve type |
| `0x03` | `SSF1` | Default mode, sensor/temperature topology, OWI, lock, charge pump |
| `0x04` | `SSF2` | DAC/AOUT, diagnostics output, auto-zero, cyclic oversampling |
| `0x05–0x13` | SSC coefficients | Sensor/temperature correction coefficients 24 bit |
| `0x14` | `SM_config1` | Main sensor PGA, ADC resolution, offset/reference |
| `0x15` | `SM_config2` | Input shift, bias current, ADC/PGA shift features |
| `0x16` | `extTemp_config1` | External temperature PGA/ADC/reference |
| `0x17` | `extTemp_config2` | External temperature shift/bias/AFE features |
| `0x18–0x1A` | `TRSH1a`, `TRSH2a` | Hai threshold chính 24 bit |
| `0x1B–0x1D` | Post-calibration shift | `SENS_Shift` và `T_Shift` 24 bit |
| `0x1E–0x20` | Measurement scheduler | Cyclic sequence và khoảng cách slot |
| `0x21` | `select_checks` | Enable các sensor/connection diagnostics |
| `0x22–0x23` | DAC production data | `DAC10RM5V`, `DAC90RM5V` do Renesas lập trình |
| `0x24` | Enhanced output | Clipping, EOC hysteresis, DAC-SSC enable |
| `0x25–0x26` | DAC clip limits | Lower/upper clipping limits |
| `0x27–0x29` | `TRSH1b`, `TRSH2b` | Threshold hysteresis bổ sung 24 bit |
| `0x2A–0x2B` | DAC-SSC coefficients | `Gain_DAC`, `Offset_DAC` |
| `0x2C–0x34` | Free customer memory | Chưa gán, có thể dùng cho metadata |
| `0x35` | Checksum | LFSR signature do lệnh `0x90` tạo |
| `0x36–0x3F` | Renesas use | Factory trim; không ghi |

## 4. Thanh ghi `0x02` — Interface configuration

| Bit | Field | Mặc định | Ý nghĩa |
|---:|---|---:|---|
| 6:0 | `Slave_Addr` | `0x00` | Địa chỉ 7 bit cho I²C và OWI |
| 8:7 | `INT_setup` | `00` | Chức năng EOC/interrupt |
| 9 | `SS_polarity` | `0` | `0`: SS active-low; `1`: active-high |
| 11:10 | `CKP_CKE` | `00` | SPI clock polarity/phase |
| 14:12 | `CYC_period` | `000` | Khoảng cập nhật cyclic |
| 15 | `SOT_curve` | `0` | `0`: parabolic; `1`: S-shaped |

### 4.1 `INT_setup`

| Giá trị | Chức năng |
|---|---|
| `00` | End-of-conversion |
| `01` | Assert khi vượt `TRSH1a`, clear khi thấp hơn lại |
| `10` | Assert khi thấp hơn `TRSH1a`, clear khi vượt lại |
| `11` | Window/dual-threshold theo `TRSH1a` và `TRSH2a` |

### 4.2 `CKP_CKE`

| Giá trị | SCLK idle | Latch data | Output data |
|---|---|---|---|
| `00` | Low | Rising | Falling |
| `01` | Low | Falling | Rising |
| `10` | High | Falling | Rising |
| `11` | High | Rising | Falling |

### 4.3 `CYC_period`

| Giá trị | Period |
|---|---:|
| `000` | 0.0 ms |
| `001` | 0.1 ms |
| `010` | 1.0 ms |
| `011` | 2.5 ms |
| `100` | 5.0 ms |
| `101` | 10 ms |
| `110` | 50 ms |
| `111` | 87.5 ms |

Lưu ý: đây là khoảng update bổ sung của cyclic operation; tổng nhịp còn phụ thuộc thời gian measurement sequence.

## 5. Thanh ghi `0x03` — SSF1

| Bit | Field | Mặc định | Ý nghĩa |
|---:|---|---:|---|
| 1:0 | `default_mode` | `00` | Command/Cyclic/Sleep sau power-on |
| 2 | `owi_su_length` | `0` | OWI startup window: 50 ms hoặc 3 ms |
| 3 | `owi_su_case` | `0` | Cách phối hợp OWI startup và AOUT |
| 6:4 | `temp_source` | `000` | Nguồn đo nhiệt độ |
| 8:7 | `sensor_sup` | `00` | Kiểu cấp nguồn/main sensor topology |
| 11:9 | `internal_rt` | `000` | Chọn internal top resistor |
| 12 | `extra_rt` | `0` | Điều khiển internal bottom resistor |
| 13 | `owi_off` | `0` | `1`: disable OWI |
| 14 | `lock` | `0` | Khóa toàn bộ NVM sau reset |
| 15 | `cp_off` | `0` | Tắt charge pump khi điều kiện nguồn cho phép |

### 5.1 `default_mode`

| Giá trị | Mode |
|---|---|
| `00` | Command |
| `01` | Cyclic |
| `10` | Sleep |
| `11` | Không gán |

Nếu chọn Sleep làm default mode, phải đặt `owi_off = 1`; tổ hợp Sleep mặc định với OWI enable không được hỗ trợ.

### 5.2 `sensor_sup`

| Giá trị | Cấu hình |
|---|---|
| `00` | Ratiometric supply tại VDDB |
| `01` | Current mode từ VDDB/Tbias |
| `10` | Absolute voltage source, ví dụ thermopile |
| `11` | Không gán |

`temp_source` có nhiều topology bridge, TEXT, diode/PTC và internal PTAT. Không chọn chỉ dựa trên tên field; phải đối chiếu sơ đồ ứng dụng và front-end configuration trong datasheet.

### 5.3 Cảnh báo `lock` và `cp_off`

- `lock = 1` có hiệu lực sau reset và không thể đảo bằng write thông thường.
- Chỉ set lock sau khi ghi, read-back, checksum và measurement validation hoàn tất.
- Chỉ đặt `cp_off = 1` khi bảo đảm `VDD > 4.3 V`; mặc định nên giữ charge pump bật để có PSRR tốt hơn.

## 6. Thanh ghi `0x04` — SSF2

| Bit | Field | Mặc định | Ý nghĩa |
|---:|---|---:|---|
| 1:0 | `dacres` | `00` | DAC 13/14/15/16 bit |
| 2 | `dither_off` | `1` | `0`: bật DAC dithering |
| 3 | `dacouttype` | `0` | `0`: sensor; `1`: temperature |
| 4 | `cont_ANAoutn` | `0` | `1`: không xuất analog |
| 7:5 | `Aout_setup` | `001`* | Current loop/ratiometric/absolute output |
| 8 | `diagouten` | `0` | Enable analog diagnostic levels |
| 9 | `disable_ldoctrl` | `0` | Disable internal LDOctrl output |
| 11:10 | `VDD_ldoctrl_target` | `10` | Target 4.8/5.0/5.2/5.4 V |
| 12 | `AZMs_on` | `0` | Auto-zero main sensor |
| 13 | `AZMt_on` | `0` | Auto-zero temperature |
| 15:14 | `oversamp_cyc` | `00` | Cyclic oversampling |

\* Một số preconfigured current-loop variants có default khác; luôn đọc NVM thực tế.

### 6.1 `oversamp_cyc`

| Giá trị | Hệ số |
|---|---:|
| `00` | Không oversample |
| `01` | ×4 |
| `10` | ×8 |
| `11` | ×16 |

Field này chỉ áp dụng Cyclic Mode. Các lệnh `0xAC–0xAF` điều khiển oversampling riêng cho Sleep/Command Mode.

## 7. Hệ số SSC `0x05–0x13`

Mỗi hệ số logic rộng 24 bit nhưng được chia giữa một word LSB 16 bit và một byte MSB dùng chung với hệ số khác.

| Hệ số | LSB `[15:0]` | MSB `[23:16]` |
|---|---:|---|
| `Offset_S` | `0x05` | `0x0F[15:8]` |
| `Gain_S` | `0x06` | `0x0F[7:0]` |
| `Tcg` | `0x07` | `0x10[15:8]` |
| `Tco` | `0x08` | `0x10[7:0]` |
| `SOT_tco` | `0x09` | `0x11[15:8]` |
| `SOT_tcg` | `0x0A` | `0x11[7:0]` |
| `SOT_sens` | `0x0B` | `0x12[15:8]` |
| `Offset_T` | `0x0C` | `0x12[7:0]` |
| `Gain_T` | `0x0D` | `0x13[15:8]` |
| `SOT_T` | `0x0E` | `0x13[7:0]` |

Ghép hệ số:

```c
uint32_t coeff_u24 = ((uint32_t)msb << 16) | lsb;
int32_t coeff_s24 = (coeff_u24 & 0x800000u)
                  ? (int32_t)(coeff_u24 | 0xFF000000u)
                  : (int32_t)coeff_u24;
```

Khi sửa một hệ số, phải read-modify-write byte dùng chung để không phá MSB của hệ số còn lại.

## 8. Main sensor configuration `0x14–0x15`

### 8.1 `0x14` — `SM_config1`

| Bit | Field | Mặc định | Ý nghĩa |
|---:|---|---:|---|
| 3:0 | `Gain_stage1` | `0111` | PGA stage 1 |
| 6:4 | `Gain_stage2` | `001` | PGA stage 2 |
| 7 | `Gain_polarity` | `0` | Đảo polarity đường sensor |
| 11:8 | `adc_bits` | `0100` | 12–24 bit; mặc định 16 bit |
| 14:12 | `adc_offset` | `000` | ADC offset compensation |
| 15 | `sel_ref1` | `1` | `0`: bandgap; `1`: ratiometric |

PGA gain:

$$
G_{PGA}=G_1\times G_2
$$

| `Gain_stage1` | Gain | `Gain_stage1` | Gain |
|---|---:|---|---:|
| `0000` | 1.2 | `1000` | 60 |
| `0001` | 2 | `1001` | 80 |
| `0010` | 4 | `1010` | 120 |
| `0011` | 6 | `1011` | 150 |
| `0100` | 12 | `1100` | 200 |
| `0101` | 20 | `1101` | 240 |
| `0110` | 30 | `1110` | 300 |
| `0111` | 40 | `1111` | Không gán |

`Gain_stage2` mã `000–111` tương ứng gain `1.1–1.8` theo bước `0.1`.

`adc_bits = 0x0–0xC` tương ứng 12–24 bit; `0xD–0xF` không gán.

### 8.2 `0x15` — `SM_config2`

| Bit | Field | Mặc định | Ý nghĩa |
|---:|---|---:|---|
| 4:0 | `ioffsc` | `00000` | Input offset shift từ −15 mV đến +15 mV |
| 7:5 | `Tbiasout` | `000` | Sensor bias current |
| 8 | `adc_en_shift` | `0` | Enable ADC ×2 và `adc_offset` |
| 9 | `pga_en_shift` | `1` | Automatic common-mode adjustment |
| 15:10 | Reserved | `0` | Phải giữ bằng `0` |

| `Tbiasout` | Dòng danh nghĩa |
|---|---:|
| `000` | 5 µA |
| `001` | 10 µA |
| `010` | 20 µA |
| `011` | 39 µA |
| `100` | 79 µA |
| `101` | 157 µA |
| `110` | 196 µA |
| `111` | 494 µA |

`ioffsc` dùng hai vùng mã: `00000` là 0 mV, `00001–01111` là −1…−15 mV; `10000` là 0 mV, `10001–11111` là +1…+15 mV.

## 9. External temperature configuration `0x16–0x17`

Hai word có cấu trúc gần giống `SM_config1/2`:

- `0x16`: `Gain_stage1`, `Gain_stage2`, polarity, `adc_bits`, `adc_offset`, `sel_ref2`.
- `0x17`: `ioffsc`, `Tbiasout`, `adc_en_shift`, `pga_en_shift`, reserved.

Chúng chỉ điều khiển external temperature path. Nếu dùng internal PTAT, IC sử dụng cấu hình factory trong vùng Renesas. Không sao chép mù cấu hình main sensor sang temperature path.

## 10. Threshold và post-calibration shift

| Giá trị 24 bit | LSB `[15:0]` | MSB `[23:16]` |
|---|---:|---|
| `TRSH1a` | `0x18` | `0x1A[7:0]` |
| `TRSH2a` | `0x19` | `0x1A[15:8]` |
| `SENS_Shift` | `0x1B` | `0x1D[15:8]` |
| `T_Shift` | `0x1C` | `0x1D[7:0]` |
| `TRSH1b` | `0x27` | `0x29[7:0]` |
| `TRSH2b` | `0x28` | `0x29[15:8]` |

Threshold phải được lập trình left-aligned theo ADC resolution. `TRSH1b/TRSH2b` chỉ có tác dụng khi `eoc_hyst_on = 1` tại `0x24[2]`.

## 11. Cyclic measurement scheduler `0x1E–0x20`

Nhóm này định nghĩa:

- có thực hiện temperature, sensor, AZT, AZS và connection check trong sequence hay không;
- số pause slots giữa các temperature measurements;
- số pause slots giữa sensor và auto-zero sensor;
- số pause slots giữa các connection checks.

Quy tắc quan trọng:

- Nếu `AZMt_on = 0`, scheduler AZT không tạo phép đo AZT.
- Nếu `AZMs_on = 0`, scheduler AZS không tạo phép đo AZS.
- `slots_S` phải bằng `slots_AZS` để Cyclic Mode vận hành đúng.
- Reserved bits của scheduler phải giữ theo giá trị datasheet.
- Tổng sample period phải tính từ cả conversion time, sequence và pause slots; không chỉ dùng `CYC_period`.

Vì scheduler phụ thuộc mạnh vào use case và có nhiều tổ hợp, production image phải lưu một bảng giải thích sequence kỳ vọng bên cạnh ba word raw.

## 12. Diagnostic selection `0x21`

| Bit | Field | Check |
|---:|---|---|
| 0 | `inp_check` | Mất kết nối INP |
| 1 | `inn_check` | Mất kết nối INN |
| 2 | `inp_range_check` | INP ngoài dải |
| 3 | `inn_range_check` | INN ngoài dải |
| 4 | `sens_short_check` | Sensor short |
| 5 | `text_open_check` | TEXT open |
| 6 | `text_range_check` | TEXT ngoài dải |
| 7 | `text_inn_short_check` | TEXT short với INN |
| 8 | `text_inp_short_check` | TEXT short với INP |
| 9 | `crack_check` | Die crack/chipping check |
| 15:10 | Reserved | Giữ `0` |

Không bật các connection/range check được datasheet cảnh báo không phù hợp với absolute-voltage sensor (`sensor_sup = 10`). Với current-loop, tránh các short checks gây biến thiên dòng nếu kiến trúc hệ thống không chấp nhận được.

## 13. DAC và enhanced output `0x22–0x2B`

- `0x22–0x23`: calibration-point data do Renesas đo/lập trình; không xem là vùng scratch.
- `0x24[1:0] clipping_on`: enable lower/upper DAC clipping.
- `0x24[2] eoc_hyst_on`: enable threshold set `b` cho hysteresis.
- `0x24[3] dac_ssc_enable`: enable `Gain_DAC`/`Offset_DAC`.
- `0x25`: `Low_Clip_Lim`.
- `0x26`: `Up_Clip_Lim`.
- `0x27–0x29`: threshold set `b`.
- `0x2A`: `Gain_DAC`.
- `0x2B`: `Offset_DAC`.

Khi analog diagnostic signaling được enable, hành vi diagnostic level có ưu tiên riêng và clipping không được áp dụng theo cách thông thường.

## 14. Free memory `0x2C–0x34`

Có thể dùng cho metadata nhỏ như:

- configuration format version;
- calibration profile ID;
- sensor lot/variant code;
- production station ID rút gọn.

Không nên lưu counter runtime hoặc event log vì giới hạn endurance và vì thay đổi bất kỳ word nào cũng yêu cầu tạo lại checksum.

Nên dành ít nhất một word cho `config_schema_version` để firmware biết cách diễn giải image.

## 15. Checksum `0x35`

Checksum NVM dùng LFSR polynomial:

$$
x^{16}+x^{15}+x^2+1
$$

Không tự ghi giá trị phỏng đoán vào `0x35`. Sau khi cập nhật NVM, gửi command `0x90` để IC tính và ghi checksum. Checksum chỉ được verify trực tiếp sau power-on; vì vậy quy trình validation phải bao gồm reset và kiểm tra status `Memory Error`.

## 16. Shadow registers

| Command | Shadow đích | NVM gốc |
|---:|---|---:|
| `0xD6` | `SM_config1` | `0x14` |
| `0xD7` | `SM_config2` | `0x15` |
| `0xD8` | `T_config1` | `0x16` hoặc factory config |
| `0xD9` | `T_config2` | `0x17` hoặc factory config |
| `0xDA` | `SSF1` | `0x03` |
| `0xDB` | `SSF2` | `0x04` |

Shadow overwrite phù hợp cho characterization, calibration và self-test. Nó không thay đổi NVM và bị xóa bởi POR/RESQ. Không dùng shadow overwrite như cấu hình production lâu dài trừ khi firmware cố ý reapply sau mỗi reset.

## 17. Quy trình tạo production image

1. Bắt đầu từ NVM dump thực tế của đúng variant IC.
2. Giữ nguyên vùng Renesas `0x36–0x3F` và dữ liệu factory tại `0x22–0x23`.
3. Chọn topology sensor/temperature tại `0x03`.
4. Chọn AFE tại `0x14–0x17` bằng raw measurement và shadow overwrite.
5. Tạo các hệ số SSC `0x05–0x13` từ calibration tool/process.
6. Cấu hình interface, mode, scheduler, diagnostics và output.
7. Ghi metadata/schema version vào vùng free nếu cần.
8. Để `lock = 0` trong giai đoạn phát triển.
9. Ghi image, read-back từng word và so sánh.
10. Gửi `0x90`, reset, kiểm tra `Memory Error = 0`.
11. Chạy measurement/diagnostic acceptance test.
12. Chỉ set lock trong bước production được kiểm soát; sau đó tạo lại checksum và reset kiểm tra lần cuối.

## 18. Quy tắc firmware

- Định nghĩa address, mask và shift riêng; không dùng C bit-field phụ thuộc compiler.
- Có helper `field_get()` và `field_set()` dùng mask rõ ràng.
- Mọi write vào word chứa hai hệ số 24 bit phải read-modify-write.
- Reserved bits phải được bảo toàn hoặc ghi đúng giá trị yêu cầu.
- Cấm write `0x36–0x3F` trong public driver API.
- Cấm ghi `0x35` trực tiếp; cung cấp API `recalculate_checksum()`.
- Tách API NVM write khỏi API shadow overwrite.
- Mọi production image phải có version, CRC/hash ngoài IC và file giải thích các field.
- Runtime không được tự động sửa NVM để recovery.

## 19. Checklist review cấu hình

- [ ] Slave address hợp lệ và không xung đột bus.
- [ ] SPI mode/SS polarity khớp firmware và boot pin state.
- [ ] Default mode tương thích OWI setting.
- [ ] Sensor supply và temperature topology khớp schematic.
- [ ] PGA/ADC không saturation ở toàn dải sensor và nhiệt độ.
- [ ] Auto-zero, oversampling và scheduler đạt update rate mục tiêu.
- [ ] Diagnostic checks phù hợp loại sensor.
- [ ] Threshold được left-align đúng ADC resolution.
- [ ] Không ghi đè DAC factory data hoặc Renesas-use region.
- [ ] Coefficient 24 bit được ghép đúng byte và sign-extend đúng.
- [ ] Reserved bits đúng giá trị.
- [ ] Read-back khớp image trước khi tạo checksum.
- [ ] `Memory Error = 0` sau reset.
- [ ] Lock vẫn bằng `0` cho đến bước production cuối.

## 20. Tài liệu tham khảo

1. [Renesas ZSSC3241 Datasheet, Rev. Feb. 2, 2024](https://www.renesas.com/en/document/dst/zssc3241-datasheet)
2. `ZSSC3241_Technical_Summary.md`
3. `ZSSC3241_Command_Notes.md`

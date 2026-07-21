---
document_id: SWFPM-FLOW-PROD-001
title: Đề xuất công thức tính lưu lượng dùng cho production
status: DRAFT_FOR_REVIEW
version: 1.0
owner: Firmware Measurement / System Engineering
last_updated: 2026-07-17
project: Smart Water Flow and Pressure Monitor
baseline_repository: whoisLePhuc/smart-water-flow-pressure-monitor
language: vi
---

# Đề xuất công thức tính lưu lượng dùng cho production

## 1. Mục đích

Tài liệu này đề xuất cách thay thế công thức lưu lượng dạng prototype hiện tại bằng một pipeline phù hợp hơn cho firmware production của hệ thống Smart Water Flow and Pressure Monitor.

Phạm vi gồm:

- thống nhất quy ước upstream/downstream;
- xây dựng công thức vật lý từ hai giá trị ToF;
- xác định đơn vị và fixed-point scale;
- bù zero-flow offset theo nhiệt độ;
- hiệu chỉnh geometry và velocity profile;
- calibration đa điểm theo từng thiết bị;
- lọc, deadband và quality gate;
- cấu trúc profile, calibration record và result;
- thay đổi `flow_compute()`;
- unit test, integration test và calibration procedure;
- tiêu chí để đánh dấu `DATA_ACCEPTED`.

> Công thức vật lý không tự động tạo ra kết quả production. Kết quả cuối cùng phải đi qua validation, compensation, calibration và verification với hệ thống đo tham chiếu.

---

# 2. Trạng thái công thức hiện tại

Implementation prototype hiện dùng logic tương đương:

```c
int64_t delta_tof;
int64_t velocity;
int64_t flow_raw;

checked_sub_i64(tof_down_ps, tof_up_ps, &delta_tof);
checked_mul_i64(delta_tof, profile->path_length, &velocity);
checked_mul_i64(velocity, profile->pipe_area, &flow_raw);
```

Công thức:

$$
Q_{raw}
=
\left(t_{down}-t_{up}\right)
\cdot L
\cdot A
$$

Công thức này phù hợp để:

- kiểm tra processing pipeline.
- kiểm tra dấu.
- kiểm tra overflow.
- tạo dữ liệu mô phỏng cho volume và leak.
- kiểm tra API calibration.

Công thức chưa dùng được cho production vì thiếu:

- tích số $t_{up}t_{down}$;
- góc acoustic path;
- zero-flow offset;
- ảnh hưởng nhiệt độ;
- hydraulic/profile correction;
- multipoint calibration;
- raw-hit filtering;
- deadband;
- kiểm tra absolute ToF;
- kiểm tra sound speed;
- unit proof.

Đơn vị của:

$$
\Delta t\cdot L\cdot A
$$

cũng không trực tiếp tương đương lưu lượng thể tích.

---

# 3. Các thông tin phải chốt trước implementation

## 3.1. Cơ khí

- đường kính trong của ống;
- tiết diện hiệu dụng;
- acoustic path length;
- acoustic path angle;
- kiểu direct path hoặc reflector;
- vị trí transducer A/B;
- chiều vật lý được coi là forward;
- tolerance của spool body.

## 3.2. MAX35103

- register configuration;
- đơn vị ToF;
- mapping upstream/downstream;
- số hit;
- hit acceptance rule;
- status/error bit;
- coherent-pair rule;
- temperature pairing rule.

## 3.3. Sản phẩm

- dải flow;
- accuracy;
- repeatability;
- minimum detectable flow;
- dải nhiệt độ;
- reverse-flow requirement;
- zero stability;
- response time;
- leak-relevant flow range.

## 3.4. Hiệu chuẩn

- reference meter;
- reference accuracy;
- số điểm flow;
- số mức nhiệt độ;
- calibration per-device hay per-batch;
- cách lưu record;
- validation point độc lập.

---

# 4. Quy ước dấu

## 4.1. Quy ước đề xuất

- `tof_down_ps`: ToF cùng chiều forward flow;
- `tof_up_ps`: ToF ngược chiều forward flow;
- forward flow làm $t_{down}<t_{up}$;
- forward flow có giá trị dương.

Định nghĩa:

$$
\Delta t_{measured}
=
t_{up}-t_{down}
$$

Khi forward flow:

$$
t_{up}>t_{down}
$$

nên:

$$
\Delta t_{measured}>0
$$

và:

$$
Q>0
$$

## 4.2. Code

```c
int64_t delta_tof_measured_ps;

if (!checked_sub_i64(
        tof_up_ps,
        tof_down_ps,
        &delta_tof_measured_ps)) {
    return FLOW_NUMERIC_ERROR;
}
```

## 4.3. Nơi phải dùng cùng quy ước

- schematic;
- transducer marking;
- assembly instruction;
- MAX35103 driver;
- raw measurement struct;
- `FlowProfile`;
- `FlowResult`;
- volume accumulator;
- telemetry;
- test vector;
- calibration report.

## 4.4. Test dấu

| Trường hợp | Quan hệ ToF | Kết quả |
|---|---|---|
| Zero lý tưởng | $t_{up}=t_{down}$ | `NONE` |
| Forward | $t_{up}>t_{down}$ | Flow dương |
| Reverse | $t_{up}<t_{down}$ | Flow âm |

---

# 5. Mô hình vật lý

## 5.1. Ký hiệu

| Ký hiệu | Ý nghĩa | Đơn vị SI |
|---|---|---|
| $L$ | Acoustic path length | m |
| $\theta$ | Góc path so với trục ống | rad/degree |
| $c$ | Vận tốc âm | m/s |
| $v$ | Vận tốc flow đại diện | m/s |
| $t_{down}$ | ToF xuôi dòng | s |
| $t_{up}$ | ToF ngược dòng | s |
| $A$ | Tiết diện ống | m² |
| $K_h$ | Hydraulic/profile factor | Không thứ nguyên |
| $Q$ | Lưu lượng thể tích | m³/s |

## 5.2. ToF xuôi dòng

$$
t_{down}
=
\frac{L}
{c+v\cos\theta}
$$

## 5.3. ToF ngược dòng

$$
t_{up}
=
\frac{L}
{c-v\cos\theta}
$$

## 5.4. Vận tốc dòng

Từ hai phương trình:

$$
\frac{1}{t_{down}}
-
\frac{1}{t_{up}}
=
\frac{2v\cos\theta}{L}
$$

Suy ra:

$$
v
=
\frac{L}
{2\cos\theta}
\left(
\frac{1}{t_{down}}
-
\frac{1}{t_{up}}
\right)
$$

Tương đương:

$$
v
=
\frac{
L\left(t_{up}-t_{down}\right)
}
{
2\cos\theta
\cdot t_{up}
\cdot t_{down}
}
$$

Với:

$$
\Delta t=t_{up}-t_{down}
$$

ta có:

$$
v
=
\frac{
L\Delta t
}
{
2\cos\theta
\cdot t_{up}
\cdot t_{down}
}
$$

## 5.5. Lưu lượng

Lưu lượng lý tưởng:

$$
Q_{ideal}=A\cdot v
$$

Thêm hydraulic factor:

$$
Q_{model}
=
K_h\cdot A\cdot v
$$

Công thức nền tảng:

$$
Q_{model}
=
K_h
\cdot A
\cdot
\frac{
L\Delta t
}
{
2\cos\theta
\cdot t_{up}
\cdot t_{down}
}
$$

---

# 6. Công thức gần đúng

Khi:

$$
v\ll c
$$

có thể dùng:

$$
t_{up}t_{down}
\approx
\frac{L^2}{c^2}
$$

Suy ra:

$$
v
\approx
\frac{
c(T)^2\Delta t
}
{
2L\cos\theta
}
$$

và:

$$
Q
\approx
K_h
\cdot A
\cdot
\frac{
c(T)^2\Delta t
}
{
2L\cos\theta
}
$$

Công thức gần đúng phụ thuộc trực tiếp vào mô hình $c(T)$ và độ chính xác của temperature pairing.

Do firmware có cả `tof_up_ps` và `tof_down_ps`, phương án ưu tiên là công thức đầy đủ dùng tích:

$$
t_{up}t_{down}
$$

Công thức gần đúng chỉ nên dùng làm:

- cross-check;
- simulator model;
- diagnostic;
- fallback đã được phê duyệt.

---

# 7. Zero-flow compensation

## 7.1. Vấn đề

Tại zero flow thực tế thường có:

$$
t_{up}-t_{down}\neq0
$$

Nguyên nhân:

- transducer A/B khác nhau;
- delay phát/thu không đối xứng;
- comparator threshold;
- spool body không đối xứng;
- lắp đặt;
- ảnh hưởng nhiệt độ;
- waveform selection.

Nếu không bù, hệ thống có thể:

- cộng volume giả;
- báo leak giả;
- đổi direction liên tục;
- sai lớn tại low flow.

## 7.2. Công thức bù

$$
\Delta t_{measured}
=
t_{up}-t_{down}
$$

Zero offset:

$$
\Delta t_{zero}
=
f_{zero}(T)
$$

Differential ToF đã bù:

$$
\Delta t_{corrected}
=
\Delta t_{measured}
-
\Delta t_{zero}(T)
$$

Công thức:

$$
Q_{model}
=
K_h
\cdot A
\cdot
\frac{
L\Delta t_{corrected}
}
{
2\cos\theta
\cdot t_{up}
\cdot t_{down}
}
$$

## 7.3. Mô hình tuyến tính

$$
\Delta t_{zero}(T)
=
aT+b
$$

Chỉ dùng khi dữ liệu calibration cho thấy gần tuyến tính.

## 7.4. LUT đề xuất

```c
typedef struct {
    int32_t temperature_mdeg_c;
    int64_t zero_offset_ps;
} FlowZeroOffsetPoint;
```

Nội suy:

$$
\Delta t_{zero}(T)
=
\Delta t_i
+
\frac{
(T-T_i)
(\Delta t_{i+1}-\Delta t_i)
}
{
T_{i+1}-T_i
}
$$

## 7.5. Ngoài miền LUT

Đề xuất:

- clamp trong một margin nhỏ;
- đặt quality flag;
- ngoài margin thì không `DATA_ACCEPTED`;
- không extrapolate không giới hạn.

---

# 8. Hydraulic/profile correction

Một acoustic path đo vận tốc trên đường truyền, không trực tiếp đo vận tốc trung bình toàn tiết diện.

Ảnh hưởng gồm:

- Reynolds number;
- laminar/turbulent profile;
- swirl;
- elbow/valve;
- roughness;
- reflector;
- mounting;
- direction.

Công thức:

$$
Q_{physical}
=
K_h
\cdot A
\cdot v_{path}
$$

Các mức triển khai:

### Mức 1

$$
K_h=K_0
$$

### Mức 2

$$
K_h=f(Q_{model})
$$

### Mức 3

$$
K_h=f(Q_{model},T)
$$

### Mức 4

$$
K_h=f(Q_{model},T,direction)
$$

Trong firmware production, ảnh hưởng này có thể được gộp vào calibration LUT thay vì duy trì một hàm $K_h$ riêng.

---

# 9. Multipoint calibration

## 9.1. Công thức tổng quát

$$
Q_{production}
=
f_{calibration}
\left(
Q_{model},
T,
direction
\right)
$$

## 9.2. Gain table

Tại điểm $i$:

$$
G_i
=
\frac{
Q_{reference,i}
}
{
Q_{model,i}
}
$$

Nội suy:

$$
G(Q)
=
G_i
+
\frac{
(G_{i+1}-G_i)
(Q-Q_i)
}
{
Q_{i+1}-Q_i
}
$$

Kết quả:

$$
Q_{production}
=
Q_{model}\cdot G(Q_{model})
$$

## 9.3. Hạn chế gần zero

Khi:

$$
Q_{model}\rightarrow0
$$

thì tỷ số:

$$
\frac{Q_{reference}}{Q_{model}}
$$

không ổn định.

## 9.4. Mapping table đề xuất

Lưu trực tiếp:

```text
Q_model → Q_reference
```

```c
typedef struct {
    int64_t model_flow_ul_per_s;
    int64_t reference_flow_ul_per_s;
} FlowCalibrationPoint;
```

Nội suy:

$$
Q_{production}
=
Q_{ref,i}
+
\frac{
(Q_{model}-Q_{model,i})
(Q_{ref,i+1}-Q_{ref,i})
}
{
Q_{model,i+1}-Q_{model,i}
}
$$

## 9.5. Tách theo chiều

```c
forward_points[]
reverse_points[]
```

Không giả định forward và reverse hoàn toàn đối xứng.

## 9.6. Theo nhiệt độ

Các lựa chọn:

1. Zero-offset LUT theo nhiệt độ và một flow LUT chung.
2. Flow LUT riêng cho từng mức nhiệt độ.
3. Surface model $f(Q,T)$.

Giai đoạn đầu nên ưu tiên lựa chọn 1. Chỉ mở rộng khi test chứng minh chưa đạt accuracy.

## 9.7. Yêu cầu LUT

- tăng đơn điệu;
- không duplicate;
- bao phủ dải;
- có version;
- có profile/device binding;
- có CRC;
- quy tắc clamp/reject;
- unit cố định.

---

# 10. Deadband và hysteresis

## 10.1. Deadband

$$
Q_{output}
=
\begin{cases}
0,
&
|Q_{calibrated}|
\le
Q_{deadband}
\\
Q_{calibrated},
&
|Q_{calibrated}|
>
Q_{deadband}
\end{cases}
$$

## 10.2. Hysteresis

Dùng hai ngưỡng:

- `zero_deadband_enter_ul_per_s`;
- `zero_deadband_clear_ul_per_s`.

Yêu cầu:

$$
Q_{enter}>Q_{clear}
$$

Ví dụ:

```text
NONE → FORWARD:
Q >= forward_enter

FORWARD → NONE:
Q <= forward_clear

NONE → REVERSE:
Q <= -reverse_enter

REVERSE → NONE:
Q >= -reverse_clear
```

Deadband phải được xác định từ zero-flow dataset, không hardcode tùy ý.

---

# 11. Lọc dữ liệu

## 11.1. Ba tầng

```text
Raw-hit filtering
→ Pair-level validation
→ Output-flow filtering
```

## 11.2. Raw-hit filtering

- status validation;
- hit count;
- hit window;
- median;
- trimmed mean;
- maximum spread;
- outlier rejection.

## 11.3. Pair validation

Một cặp ToF cần:

- cùng generation;
- cùng sample sequence;
- cùng config/profile;
- completion không stale;
- cả hai chiều hợp lệ;
- temperature pairing hợp lệ;
- ToF trong range;
- delta trong range.

## 11.4. Output filtering

Có thể dùng:

- IIR nhẹ;
- median window ngắn;
- moving average ngắn;
- rate limit.

Không nên lọc quá mạnh vì có thể:

- che burst;
- làm chậm leak detection;
- gây volume lag;
- tăng phase delay.

Đề xuất:

- lọc mạnh ở raw-hit level;
- output filter nhẹ;
- leak dùng persistence duration riêng;
- diagnostic build lưu cả raw/model/calibrated/filtered.

---

# 12. Kiểm tra vận tốc âm

## 12.1. Tính từ ToF

$$
c_{measured}
=
\frac{L}{2}
\left(
\frac{1}{t_{down}}
+
\frac{1}{t_{up}}
\right)
$$

## 12.2. Giá trị kỳ vọng

$$
c_{expected}
=
f_c(T)
$$

Sai khác:

$$
e_c
=
c_{measured}
-
c_{expected}
$$

## 12.3. Mục đích

Phát hiện:

- bọt khí;
- ống không đầy;
- sai hit;
- coupling kém;
- sai geometry;
- temperature mismatch;
- transducer lỗi;
- register config lỗi.

## 12.4. Quality gate

```c
if (abs(c_measured - c_expected) >
    profile->maximum_sound_speed_error_mm_per_s) {
    quality_flags |=
        FLOW_QUALITY_SOUND_SPEED_IMPLAUSIBLE;

    return FLOW_SOUND_SPEED_IMPLAUSIBLE;
}
```

---

# 13. Fixed-point và đơn vị

## 13.1. Đơn vị đề xuất

| Đại lượng | Đơn vị |
|---|---|
| ToF | ps |
| Delta ToF | ps |
| Acoustic path | µm |
| Pipe area | µm² |
| Flow | µL/s |
| Temperature | m°C |
| $\cos\theta$ | Q30 |
| $K_h$ | Q30 |

## 13.2. Chuyển đổi

Công thức:

$$
Q
=
K_h
A
\frac{
L\Delta t
}
{
2\cos\theta
t_{up}t_{down}
}
$$

Với unit firmware:

$$
\frac{
\mu m^2
\cdot
\mu m
\cdot
ps
}
{
ps^2
}
=
\frac{
\mu m^3
}
{
ps
}
$$

Ta có:

$$
1\ \mu L
=
10^9\ \mu m^3
$$

và:

$$
1\ s
=
10^{12}\ ps
$$

Do đó:

$$
1\ \frac{\mu m^3}{ps}
=
10^3\ \frac{\mu L}{s}
$$

Công thức fixed-unit:

$$
Q_{\mu L/s}
=
\frac{
1000
\cdot
K_h
\cdot
A_{\mu m^2}
\cdot
L_{\mu m}
\cdot
\Delta t_{ps}
}
{
2
\cdot
\cos\theta
\cdot
t_{up,ps}
\cdot
t_{down,ps}
}
$$

---

# 14. Overflow và rounding

## 14.1. Nguy cơ

Tử số:

$$
1000
\cdot A
\cdot L
\cdot\Delta t
\cdot K_h
$$

Mẫu số:

$$
2
\cdot\cos\theta
\cdot t_{up}
\cdot t_{down}
$$

có thể overflow `int64_t`.

## 14.2. Reference implementation

Dùng `__int128` trên Linux:

```c
__int128 numerator;
__int128 denominator;
```

Mục tiêu:

- tạo golden result;
- chứng minh unit;
- kiểm tra sai số;
- so sánh với STM32 implementation.

## 14.3. STM32

Các lựa chọn:

1. `__int128` bằng compiler runtime.
2. Rút gọn tử/mẫu trước khi nhân.
3. Precomputed coefficient.
4. Custom 128-bit arithmetic.
5. Scale theo từng bước.

## 14.4. Coefficient

$$
K_{geometry}
=
\frac{
1000K_hAL
}
{
2\cos\theta
}
$$

Khi đó:

$$
Q
=
K_{geometry}
\frac{
\Delta t
}
{
t_{up}t_{down}
}
$$

Coefficient phải có:

- unit;
- Q-format;
- version;
- profile ID;
- CRC;
- range proof.

## 14.5. Rounding

Mọi phép chia phải chỉ rõ policy.

Đề xuất:

- round-to-nearest;
- đối xứng cho số âm;
- test tie cases;
- không dùng implicit truncation.

---

# 15. `FlowProfile` đề xuất

```c
typedef enum {
    FLOW_SIGN_UP_MINUS_DOWN_POSITIVE_FORWARD = 0
} FlowSignConvention;

typedef struct {
    uint32_t schema_version;
    uint32_t profile_id;
    uint32_t profile_version;

    uint64_t acoustic_path_um;
    uint64_t pipe_area_um2;

    int32_t cos_theta_q30;
    int32_t hydraulic_factor_q30;

    int64_t minimum_tof_ps;
    int64_t maximum_tof_ps;
    int64_t maximum_abs_delta_tof_ps;

    int64_t maximum_forward_flow_ul_per_s;
    int64_t maximum_reverse_flow_ul_per_s;

    int64_t zero_deadband_enter_ul_per_s;
    int64_t zero_deadband_clear_ul_per_s;

    int32_t minimum_temperature_mdeg_c;
    int32_t maximum_temperature_mdeg_c;

    int32_t minimum_sound_speed_mm_per_s;
    int32_t maximum_sound_speed_mm_per_s;
    int32_t maximum_sound_speed_error_mm_per_s;

    uint32_t transducer_a_id;
    uint32_t transducer_b_id;
    uint32_t spool_body_id;

    FlowSignConvention sign_convention;
} FlowProfile;
```

Validation:

- area/path khác 0;
- `cos_theta_q30` hợp lệ;
- min ToF < max ToF;
- delta range hợp lệ;
- deadband clear <= enter;
- temperature range hợp lệ;
- identity đầy đủ;
- sign convention được hỗ trợ.

---

# 16. `FlowCalibrationRecord` đề xuất

```c
#define FLOW_MAX_ZERO_POINTS  16u
#define FLOW_MAX_CAL_POINTS   32u

typedef struct {
    int32_t temperature_mdeg_c;
    int64_t zero_offset_ps;
} FlowZeroOffsetPoint;

typedef struct {
    int64_t model_flow_ul_per_s;
    int64_t reference_flow_ul_per_s;
} FlowCalibrationPoint;

typedef struct {
    uint32_t schema_version;
    uint32_t calibration_version;

    uint32_t device_id;
    uint32_t profile_id;
    uint32_t profile_version;
    uint32_t transducer_pair_id;
    uint32_t spool_body_id;

    uint16_t zero_offset_count;
    FlowZeroOffsetPoint
        zero_offset_points[FLOW_MAX_ZERO_POINTS];

    uint16_t forward_count;
    FlowCalibrationPoint
        forward_points[FLOW_MAX_CAL_POINTS];

    uint16_t reverse_count;
    FlowCalibrationPoint
        reverse_points[FLOW_MAX_CAL_POINTS];

    int64_t zero_deadband_enter_ul_per_s;
    int64_t zero_deadband_clear_ul_per_s;

    uint32_t calibration_flags;
    int64_t calibrated_wall_time_s;

    uint32_t crc32;
} FlowCalibrationRecord;
```

Validation:

- schema;
- CRC;
- profile binding;
- transducer/spool identity;
- point count;
- monotonic temperature;
- monotonic model flow;
- no duplicate;
- dải bao phủ;
- nội suy không overflow.

---

# 17. `FlowCandidate` và `FlowResult`

## 17.1. Candidate

```c
typedef struct {
    int64_t measured_delta_tof_ps;
    int64_t zero_offset_ps;
    int64_t corrected_delta_tof_ps;

    int64_t model_flow_ul_per_s;
    int64_t calibrated_flow_ul_per_s;
    int64_t filtered_flow_ul_per_s;

    int32_t paired_temperature_mdeg_c;
    int32_t measured_sound_speed_mm_per_s;
    int32_t expected_sound_speed_mm_per_s;

    FlowDirection direction;

    uint32_t compensation_flags;
    uint32_t processing_flags;
    uint32_t quality_flags;
} FlowCandidate;
```

Candidate không tự:

- ghi repository;
- post event;
- cộng volume;
- cập nhật leak;
- gửi telemetry.

## 17.2. Result

```c
typedef struct {
    ResultMetadata meta;

    int64_t flow_ul_per_s;
    FlowDirection direction;

    uint32_t compensation_flags;
    uint32_t processing_flags;
    uint32_t quality_flags;

    uint64_t paired_temperature_sequence;

    uint32_t flow_profile_id;
    uint32_t flow_profile_version;
    uint32_t flow_calibration_version;
} FlowResult;
```

Diagnostic build có thể thêm:

- measured delta;
- corrected delta;
- model flow;
- sound speed;
- hit spread.

---

# 18. Status và flags

## 18.1. Status

```c
typedef enum {
    FLOW_OK = 0,
    FLOW_INVALID_ARGUMENT,
    FLOW_INVALID_PROFILE,
    FLOW_INVALID_CALIBRATION,
    FLOW_INVALID_TOF,
    FLOW_TOF_OUT_OF_RANGE,
    FLOW_DELTA_OUT_OF_RANGE,
    FLOW_TEMPERATURE_UNAVAILABLE,
    FLOW_ZERO_COMPENSATION_UNAVAILABLE,
    FLOW_SOUND_SPEED_IMPLAUSIBLE,
    FLOW_CALIBRATION_OUT_OF_RANGE,
    FLOW_NUMERIC_ERROR,
    FLOW_INTERNAL_ERROR
} FlowProcessStatus;
```

## 18.2. Quality flags

```c
enum {
    FLOW_QUALITY_NONE                       = 0u,
    FLOW_QUALITY_TOF_NEAR_LIMIT             = 1u << 0,
    FLOW_QUALITY_DELTA_NEAR_LIMIT           = 1u << 1,
    FLOW_QUALITY_ZERO_OFFSET_CLAMPED         = 1u << 2,
    FLOW_QUALITY_CALIBRATION_CLAMPED         = 1u << 3,
    FLOW_QUALITY_TEMPERATURE_STALE           = 1u << 4,
    FLOW_QUALITY_SOUND_SPEED_IMPLAUSIBLE     = 1u << 5,
    FLOW_QUALITY_HIT_SPREAD_HIGH             = 1u << 6,
    FLOW_QUALITY_FILTER_ACTIVE               = 1u << 7,
    FLOW_QUALITY_DEADBAND_APPLIED            = 1u << 8,
    FLOW_QUALITY_REVERSE_CALIBRATION_MISSING = 1u << 9
};
```

## 18.3. Compensation flags

```c
enum {
    FLOW_COMP_NONE               = 0u,
    FLOW_COMP_ZERO_OFFSET        = 1u << 0,
    FLOW_COMP_ZERO_OFFSET_TEMP   = 1u << 1,
    FLOW_COMP_HYDRAULIC_FACTOR   = 1u << 2,
    FLOW_COMP_MULTIPOINT_CAL     = 1u << 3,
    FLOW_COMP_DIRECTION_SPECIFIC = 1u << 4
};
```

---

# 19. Pipeline `flow_compute()`

## 19.1. API

```c
FlowProcessStatus flow_compute(
    int64_t tof_up_ps,
    int64_t tof_down_ps,
    int32_t paired_temp_mdeg_c,
    const FlowProfile *profile,
    const FlowCalibrationRecord *calibration,
    FlowCandidate *candidate);
```

## 19.2. Trình tự

```text
1. Validate argument
2. Validate profile
3. Validate calibration binding
4. Validate absolute ToF
5. Compute canonical delta
6. Validate delta range
7. Interpolate zero offset
8. Compute corrected delta
9. Compute sound speed
10. Validate sound speed
11. Compute physical model flow
12. Apply direction-specific calibration
13. Apply output filter
14. Apply deadband/hysteresis
15. Determine direction
16. Populate flags
17. Return candidate
```

## 19.3. Pseudocode

```c
FlowProcessStatus flow_compute(
    int64_t tof_up_ps,
    int64_t tof_down_ps,
    int32_t paired_temp_mdeg_c,
    const FlowProfile *profile,
    const FlowCalibrationRecord *cal,
    FlowCandidate *candidate)
{
    if (profile == NULL ||
        cal == NULL ||
        candidate == NULL) {
        return FLOW_INVALID_ARGUMENT;
    }

    memset(candidate, 0, sizeof(*candidate));

    if (!flow_profile_is_valid(profile)) {
        return FLOW_INVALID_PROFILE;
    }

    if (!flow_calibration_is_valid_for_profile(
            cal,
            profile)) {
        return FLOW_INVALID_CALIBRATION;
    }

    if (!flow_tof_is_in_range(tof_up_ps, profile) ||
        !flow_tof_is_in_range(tof_down_ps, profile)) {
        return FLOW_TOF_OUT_OF_RANGE;
    }

    int64_t measured_delta_ps;

    if (!checked_sub_i64(
            tof_up_ps,
            tof_down_ps,
            &measured_delta_ps)) {
        return FLOW_NUMERIC_ERROR;
    }

    if (!flow_delta_is_in_range(
            measured_delta_ps,
            profile)) {
        return FLOW_DELTA_OUT_OF_RANGE;
    }

    int64_t zero_offset_ps;

    FlowProcessStatus status =
        flow_zero_offset_interpolate(
            paired_temp_mdeg_c,
            cal,
            &zero_offset_ps,
            &candidate->quality_flags);

    if (status != FLOW_OK) {
        return status;
    }

    int64_t corrected_delta_ps;

    if (!checked_sub_i64(
            measured_delta_ps,
            zero_offset_ps,
            &corrected_delta_ps)) {
        return FLOW_NUMERIC_ERROR;
    }

    int32_t measured_sound_speed_mm_per_s;

    status = flow_compute_sound_speed(
        tof_up_ps,
        tof_down_ps,
        profile,
        &measured_sound_speed_mm_per_s);

    if (status != FLOW_OK) {
        return status;
    }

    int32_t expected_sound_speed_mm_per_s;

    status = flow_expected_sound_speed(
        paired_temp_mdeg_c,
        &expected_sound_speed_mm_per_s);

    if (status != FLOW_OK) {
        return status;
    }

    if (!flow_sound_speed_is_plausible(
            measured_sound_speed_mm_per_s,
            expected_sound_speed_mm_per_s,
            profile)) {
        candidate->quality_flags |=
            FLOW_QUALITY_SOUND_SPEED_IMPLAUSIBLE;

        return FLOW_SOUND_SPEED_IMPLAUSIBLE;
    }

    int64_t model_flow_ul_per_s;

    status = flow_compute_physical_model(
        tof_up_ps,
        tof_down_ps,
        corrected_delta_ps,
        profile,
        &model_flow_ul_per_s);

    if (status != FLOW_OK) {
        return status;
    }

    int64_t calibrated_flow_ul_per_s;

    status = flow_apply_calibration(
        model_flow_ul_per_s,
        cal,
        &calibrated_flow_ul_per_s,
        &candidate->quality_flags);

    if (status != FLOW_OK) {
        return status;
    }

    int64_t filtered_flow_ul_per_s;

    status = flow_apply_output_filter(
        calibrated_flow_ul_per_s,
        &filtered_flow_ul_per_s,
        &candidate->quality_flags);

    if (status != FLOW_OK) {
        return status;
    }

    filtered_flow_ul_per_s =
        flow_apply_zero_deadband(
            filtered_flow_ul_per_s,
            profile,
            &candidate->quality_flags);

    candidate->measured_delta_tof_ps =
        measured_delta_ps;

    candidate->zero_offset_ps =
        zero_offset_ps;

    candidate->corrected_delta_tof_ps =
        corrected_delta_ps;

    candidate->model_flow_ul_per_s =
        model_flow_ul_per_s;

    candidate->calibrated_flow_ul_per_s =
        calibrated_flow_ul_per_s;

    candidate->filtered_flow_ul_per_s =
        filtered_flow_ul_per_s;

    candidate->paired_temperature_mdeg_c =
        paired_temp_mdeg_c;

    candidate->measured_sound_speed_mm_per_s =
        measured_sound_speed_mm_per_s;

    candidate->expected_sound_speed_mm_per_s =
        expected_sound_speed_mm_per_s;

    candidate->direction =
        flow_direction_from_value(
            filtered_flow_ul_per_s);

    candidate->compensation_flags |=
        FLOW_COMP_ZERO_OFFSET |
        FLOW_COMP_ZERO_OFFSET_TEMP |
        FLOW_COMP_HYDRAULIC_FACTOR |
        FLOW_COMP_MULTIPOINT_CAL;

    return FLOW_OK;
}
```

---

# 20. Physical model helper

## 20.1. Công thức

$$
Q_{\mu L/s}
=
\frac{
1000
\cdot K_h
\cdot A_{\mu m^2}
\cdot L_{\mu m}
\cdot \Delta t_{corrected,ps}
}
{
2
\cdot \cos\theta
\cdot t_{up,ps}
\cdot t_{down,ps}
}
$$

## 20.2. Reference pseudocode

```c
static FlowProcessStatus
flow_compute_physical_model(
    int64_t tof_up_ps,
    int64_t tof_down_ps,
    int64_t corrected_delta_ps,
    const FlowProfile *profile,
    int64_t *flow_ul_per_s_out)
{
    if (profile == NULL ||
        flow_ul_per_s_out == NULL) {
        return FLOW_INVALID_ARGUMENT;
    }

    __int128 numerator = 1000;
    numerator *= (__int128)profile->pipe_area_um2;
    numerator *= (__int128)profile->acoustic_path_um;
    numerator *= (__int128)corrected_delta_ps;
    numerator *=
        (__int128)profile->hydraulic_factor_q30;

    __int128 denominator = 2;
    denominator *= (__int128)tof_up_ps;
    denominator *= (__int128)tof_down_ps;
    denominator *=
        (__int128)profile->cos_theta_q30;

    if (denominator == 0) {
        return FLOW_NUMERIC_ERROR;
    }

    __int128 result =
        divide_round_nearest_i128(
            numerator,
            denominator);

    if (result > INT64_MAX ||
        result < INT64_MIN) {
        return FLOW_NUMERIC_ERROR;
    }

    *flow_ul_per_s_out = (int64_t)result;
    return FLOW_OK;
}
```

Hai Q30 của $K_h$ và $\cos\theta$ triệt tiêu scale nếu đặt đối xứng như trên.

---

# 21. Metadata và acceptance

## 21.1. `DATA_ACCEPTED`

Chỉ accepted khi:

- production purpose;
- live-device origin;
- measured provenance;
- generation hợp lệ;
- ToF pair coherent;
- status hợp lệ;
- ToF trong range;
- temperature đủ mới;
- zero compensation có sẵn;
- sound speed hợp lý;
- profile/calibration binding đúng;
- không overflow;
- calibration trong dải;
- quality policy cho phép.

## 21.2. `DEGRADED_NOT_ACCEPTED`

Ví dụ:

- temperature gần stale;
- zero LUT bị clamp;
- calibration bị clamp;
- reverse calibration thiếu;
- hit spread cao;
- fallback coefficient.

## 21.3. `REJECTED`

Ví dụ:

- stale completion;
- invalid status;
- ToF phi vật lý;
- overflow;
- profile mismatch;
- calibration CRC lỗi;
- sound speed sai lớn;
- temperature ngoài miền.

## 21.4. Downstream side effect

Chỉ `DATA_ACCEPTED` mới được:

- cộng volume;
- tạo leak production evidence;
- tạo telemetry production;
- cập nhật state có ý nghĩa đo lường.

---

# 22. Quy trình calibration

## 22.1. Chuẩn bị

- hardware revision;
- transducer pair;
- spool body;
- firmware commit;
- profile version;
- reference instrument;
- môi trường;
- ống đầy;
- không bọt khí;
- straight-pipe condition.

## 22.2. Zero-flow calibration

1. Đặt flow bằng 0.
2. Chờ ổn định.
3. Lấy nhiều ToF pair.
4. Loại status lỗi.
5. Loại outlier.
6. Tính mean/median delta.
7. Ghi temperature.
8. Lặp ở nhiều nhiệt độ.
9. Tạo zero-offset LUT.
10. Validation tại nhiệt độ trung gian.

## 22.3. Flow calibration

1. Đặt reference flow.
2. Chờ ổn định.
3. Thu ToF và temperature.
4. Tính `Q_model`.
5. Ghi `Q_reference`.
6. Tính error.
7. Lặp nhiều sample.
8. Tạo mapping LUT.
9. Kiểm tra monotonic.
10. Validation bằng điểm độc lập.

## 22.4. Phân bố điểm

Tập trung điểm tại:

- zero;
- minimum flow;
- low-flow transition;
- leak-relevant range;
- nominal flow;
- high flow;
- maximum flow.

## 22.5. Forward và reverse

- calibration forward đầy đủ;
- reverse theo requirement;
- không giả định đối xứng;
- xác minh direction riêng.

## 22.6. Nhiệt độ

Tối thiểu:

- thấp;
- phòng;
- cao.

## 22.7. Chỉ số

- absolute error;
- relative error;
- MAE;
- RMSE;
- maximum error;
- repeatability;
- zero stability;
- direction accuracy;
- temperature drift;
- device-to-device spread.

---

# 23. Test strategy

## 23.1. Unit test formula

- zero delta;
- positive delta;
- negative delta;
- equal ToF;
- min/max ToF;
- min/max delta;
- denominator zero;
- numerator overflow;
- negative rounding;
- Q30 ratio;
- unit conversion.

## 23.2. Zero-offset test

- exact point;
- interpolation;
- below range;
- above range;
- duplicate temperature;
- non-monotonic LUT;
- stale temperature;
- sign crossing.

## 23.3. Calibration test

- exact point;
- interpolation;
- forward;
- reverse;
- zero;
- clamp;
- reject;
- duplicate model flow;
- non-monotonic;
- CRC mismatch;
- binding mismatch.

## 23.4. Sound-speed test

- nominal;
- low/high temperature;
- implausible ToF;
- bubble scenario;
- wrong geometry;
- temperature mismatch.

## 23.5. Metadata test

- production accepted;
- service sample rejected;
- stale rejected;
- generation mismatch;
- calibration mismatch;
- degraded quality.

## 23.6. Integration test

```text
MAX raw
→ parser
→ TemperatureResult
→ FlowCandidate
→ FlowResult
→ RepoWriteTxn
→ RuntimeSnapshot
→ VolumeAccumulator
→ LeakDetection
```

## 23.7. Deterministic replay

Cùng trace phải tạo:

- cùng flow;
- cùng direction;
- cùng flags;
- cùng volume delta;
- cùng leak transition;
- cùng snapshot version sequence.

---

# 24. Golden vector

```yaml
case_id: FLOW-GOLDEN-001
profile_id: 1
calibration_version: 3
tof_up_ps: 50000000000
tof_down_ps: 49999999000
temperature_mdeg_c: 25000

expected:
  measured_delta_tof_ps: 1000
  zero_offset_ps: 100
  corrected_delta_tof_ps: 900
  model_flow_ul_per_s: TBD
  production_flow_ul_per_s: TBD
  direction: FORWARD
  acceptance: ACCEPTED
  quality_flags: 0
```

Golden vector nên được tạo bằng:

- Python/MATLAB reference;
- spreadsheet độc lập;
- analytical case;
- calibration rig data.

---

# 25. Acceptance criteria

## 25.1. Formula

- derivation được review;
- sign được chốt;
- unit proof được review;
- overflow proof;
- reference implementation pass.

## 25.2. Profile và calibration

- schema versioned;
- CRC;
- binding;
- LUT validation;
- restore test;
- calibration procedure.

## 25.3. Firmware

- không undefined behavior;
- warnings-as-errors;
- sanitizer pass;
- deterministic replay;
- STM32 timing đạt budget;
- no heap;
- bounded execution.

## 25.4. Measurement

- đạt accuracy;
- đạt repeatability;
- zero stability;
- reverse direction đúng;
- nhiệt độ đạt yêu cầu;
- nhiều thiết bị mẫu.

## 25.5. System

- volume error đạt target;
- leak false alarm đạt target;
- degraded không tạo side effect;
- power-cycle đúng;
- calibration restore đúng;
- trace đầy đủ.

---

# 26. Lộ trình thay đổi code

## Giai đoạn 1 — Contract

- chốt sign;
- chốt unit;
- mở rộng `FlowProfile`;
- tạo `FlowCalibrationRecord`;
- tạo enum/flag;
- viết reference implementation.

## Giai đoạn 2 — Formula

- thêm physical model helper;
- thêm zero-offset interpolation;
- thêm calibration interpolation;
- thêm sound-speed check;
- thêm test.

## Giai đoạn 3 — MAX35103

- parse coherent payload;
- raw-hit filtering;
- temperature pairing;
- generation/correlation;
- gọi `flow_compute()`.

## Giai đoạn 4 — Product integration

- build `FlowResult`;
- repo transaction;
- volume admission;
- leak admission;
- telemetry.

## Giai đoạn 5 — Calibration và optimization

- chạy rig;
- tạo LUT;
- validation;
- đo runtime STM32;
- tối ưu fixed-point;
- so sánh optimized với reference.

---

# 27. Các quyết định còn mở

| ID | Quyết định | Owner |
|---|---|---|
| FLOW-DEC-001 | Sign convention | System/Firmware |
| FLOW-DEC-002 | Đơn vị ToF | Firmware |
| FLOW-DEC-003 | $L$, $A$, $\theta$ | Mechanical/System |
| FLOW-DEC-004 | $K_h$ riêng hay gộp LUT | Calibration/System |
| FLOW-DEC-005 | Zero model tuyến tính hay LUT | Calibration |
| FLOW-DEC-006 | Số mức nhiệt độ | Product/Calibration |
| FLOW-DEC-007 | Số điểm flow | Product/Calibration |
| FLOW-DEC-008 | Reverse accuracy | Product |
| FLOW-DEC-009 | Deadband/hysteresis | Calibration |
| FLOW-DEC-010 | Sound-speed policy | System |
| FLOW-DEC-011 | `__int128` trên STM32 | Firmware |
| FLOW-DEC-012 | Clamp hay reject ngoài LUT | Product/System |
| FLOW-DEC-013 | Output filter | Firmware |
| FLOW-DEC-014 | Per-device hay per-batch | Manufacturing |

---

# 28. Công thức production tổng hợp

$$
\Delta t_{measured}
=
t_{up}-t_{down}
$$

$$
\Delta t_{corrected}
=
\Delta t_{measured}
-
\Delta t_{zero}(T)
$$

$$
Q_{model}
=
K_h
\cdot A
\cdot
\frac{
L\Delta t_{corrected}
}
{
2\cos\theta
\cdot t_{up}
\cdot t_{down}
}
$$

$$
Q_{calibrated}
=
f_{calibration}
\left(
Q_{model},
T,
direction
\right)
$$

$$
Q_{production}
=
f_{filter/deadband}
\left(
Q_{calibrated}
\right)
$$

Công thức gộp:

$$
Q_{production}
=
f_{filter/deadband}
\left[
f_{calibration}
\left(
K_h
A
\frac{
L
\left[
(t_{up}-t_{down})
-
\Delta t_{zero}(T)
\right]
}
{
2\cos\theta
t_{up}t_{down}
},
T,
direction
\right)
\right]
$$

---

# 29. Kết luận

Công thức prototype:

$$
Q
=
\Delta t
\cdot L
\cdot A
$$

cần được thay bằng pipeline:

```text
Coherent upstream/downstream ToF
→ validation
→ zero-flow temperature compensation
→ physical equation
→ hydraulic/profile correction
→ direction-specific multipoint calibration
→ filtering/deadband
→ quality and acceptance gate
→ FlowResult
```

Công thức vật lý đề xuất:

$$
Q_{model}
=
K_h
A
\frac{
L
\left[
(t_{up}-t_{down})
-
\Delta t_{zero}(T)
\right]
}
{
2\cos\theta
t_{up}t_{down}
}
$$

Kết quả production:

$$
Q_{production}
=
f_{calibration}
\left(
Q_{model},
T,
direction
\right)
$$

Chỉ khi formula, compensation, calibration, fixed-point implementation và hardware validation đạt tiêu chí thì `FlowResult` mới được đánh dấu `DATA_ACCEPTED` để dùng cho:

- volume;
- leak detection;
- LCD production;
- telemetry;
- các production side effect khác.

---

# 30. Tài liệu tham chiếu cần liên kết

- MAX35103 datasheet và register documentation.
- Application note về ultrasonic flow meter dùng MAX3510x.
- Tài liệu calibration flow meter.
- Tài liệu transducer compensation.
- Mechanical drawing của spool body.
- Calibration rig specification.
- SWFPM system requirements.
- Flow profile và calibration source-of-truth trong repository.

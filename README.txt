E-COMMERCE ORDER SYSTEM V4

Folder layout:
  main.c         source code (multi-threaded order processing with fine-grained per-product locking)
  Makefile       standard build automation (make, make run, make test, make benchmark, make clean)
  run.sh         builds and runs the program (supports CLI arguments)
  benchmark.sh   script to run multi-threaded benchmark suite
  input/         product and order data files
  output/        result files and transaction logs are written here (auto-created)

================================================================================
CHANGE OF BY-9126
================================================================================

1. CÁC CÔNG VIỆC ĐÃ HOÀN THÀNH (TASKS COMPLETED)
--------------------------------------------------------------------------------
* TASK 1: Sửa lỗi tồn đọng & Hoàn thiện trải nghiệm (Bug Fixes & UX)
  - Tự động tạo thư mục 'output/' và 'input/' đa nền tảng (Linux & Windows)
    thông qua <sys/stat.h> (mkdir), giải quyết triệt để lỗi "No such file or directory".
  - Hoàn thiện tính năng Mục 6 (View Results Content): Cho phép người dùng chọn
    và hiển thị trực tiếp nội dung các file kết quả ngay trên console thay vì chỉ in đường dẫn.
  - Đồng bộ hóa tiêu đề giao diện chuẩn xác thành 'E-COMMERCE ORDER SYSTEM V4'.

* TASK 2: Nâng cấp kiến trúc Đa luồng & Đồng bộ hóa (Fine-grained Concurrency)
  - Khóa chi tiết theo từng sản phẩm (Fine-grained per-product locking):
    Tích hợp 'pthread_mutex_t lock' vào từng struct Product. Khi xử lý đơn hàng
    cho sản phẩm này, các luồng khác xử lý sản phẩm khác vẫn chạy song song 100%
    mà không bị khóa chéo (loại bỏ nghẽn cổ chai của single global lock).
  - Cấu hình số lượng Worker Threads động:
    Cho phép người dùng tùy chỉnh từ 1 đến 32 luồng xử lý (mặc định 4) qua Mục 9.
    Quản lý bộ nhớ luồng động bằng malloc() và giải phóng bằng free().
  - Xử lý xung đột tồn kho (Stock contention) chính xác tuyệt đối, ngăn chặn
    Race Condition và overselling.

* TASK 3: Bổ sung tính năng thực tế cho hệ thống E-Commerce (Logging & Analytics)
  - Ghi nhật ký giao dịch an toàn đa luồng (Thread-Safe Transaction Log):
    Ghi nhận toàn bộ giao dịch vào 'output/transactions.log' kèm Timestamp
    thực tế (YYYY-MM-DD HH:MM:SS), Worker ID xử lý, Order ID, trạng thái đơn
    và tồn kho tức thời. Được bảo vệ an toàn bằng 'log_lock'.
  - Báo cáo phân tích doanh số & tồn kho (Sales & Inventory Analytics):
    Tự động tính toán Tỷ lệ hoàn thành đơn (Fulfillment Rate %), Tổng số lượng
    sản phẩm đã bán, và Sản phẩm bán chạy nhất (Top Seller).
  - Tích hợp xem trực tiếp Transaction Log qua Mục 6 -> Lựa chọn 9.

* TASK 4: Tự động hóa, Tham số dòng lệnh & Đối chuẩn hiệu năng (Automation & CLI)
  - Xây dựng 'Makefile' chuẩn:
    Cung cấp các lệnh biên dịch tối ưu (-O2 -pthread) và kiểm thử nhanh.
  - Hỗ trợ tham số dòng lệnh (CLI Arguments):
    Hỗ trợ chạy trực tiếp không cần tương tác menu qua ./main --benchmark,
    ./main --test, ./main --help.
  - Công cụ đo lường định luật Amdahl (Threading Performance Benchmark):
    Thêm Mục 10 trong menu và script 'benchmark.sh', tự động đo thời gian chạy,
    tính Tỷ lệ tăng tốc (Speedup) và Hiệu suất luồng (Efficiency %) qua các
    mức 1, 2, 4, 8, 16 luồng, xuất bảng ASCII trực quan.


2. CÁC BƯỚC CHẠY CHƯƠNG TRÌNH (EXECUTION STEPS)
--------------------------------------------------------------------------------

Cách 1: Sử dụng Makefile (Khuyên dùng trên Linux / Ubuntu)
  - Bước 1: Biên dịch chương trình:
      make
  - Bước 2: Chạy kiểm thử tự động (Scenario 1):
      make test
  - Bước 3: Chạy chương trình tương tác đầy đủ:
      make run
  - Bước 4: Chạy đo đạc đối chuẩn tốc độ đa luồng (Amdahl's Law Benchmark):
      make benchmark
  - Bước 5: Dọn dẹp các file thực thi và log cũ:
      make clean

Cách 2: Sử dụng Script Shell (run.sh & benchmark.sh)
  - Cấp quyền thực thi (nếu cần):
      chmod +x run.sh benchmark.sh
  - Chạy chương trình giao diện tương tác:
      bash run.sh
  - Chạy bộ kiểm thử Benchmark:
      bash benchmark.sh

Cách 3: Chạy trực tiếp với tham số dòng lệnh (CLI Flags)
  - Chạy benchmark kịch bản 3 (hoặc 1, 2):
      ./main --benchmark 3
  - Chạy kiểm thử nhanh kịch bản 1:
      ./main --test
  - Xem danh sách hướng dẫn lệnh:
      ./main --help

Cách 4: Thao tác trên Menu tương tác của chương trình
  Khi khởi động './main', chọn các phím số:
    1: Xem danh mục sản phẩm và tồn kho hiện tại.
    2: Thêm một đơn hàng mới vào hàng đợi chờ xử lý.
    3: Xem danh sách các đơn hàng đang chờ trong hàng đợi.
    4: Xử lý các đơn hàng tùy biến theo chế độ Tuần tự (Sequential).
    5: Xử lý các đơn hàng tùy biến theo chế độ Đa luồng (Multithread).
    6: Xem nội dung chi tiết các file kết quả hoặc nhật ký transactions.log.
    7: Chạy 3 kịch bản kiểm thử mẫu (Scenario 1, 2, 3) để so sánh tốc độ.
    8: Điều chỉnh tăng/giảm số lượng tồn kho của từng sản phẩm.
    9: Cấu hình thay đổi số lượng Worker Threads (1 đến 32 luồng).
    10: Chạy đo đạc kiểm thử tự động Amdahl's Law (Speedup / Efficiency).
    0: Thoát chương trình.

--------------------------------------------------------------------------------
Yêu cầu môi trường:
  Nếu máy ảo thiếu gcc hoặc make:
    sudo apt update && sudo apt install -y build-essential

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(d) _mkdir(d)
#else
#define make_dir(d) mkdir(d, 0777)
#endif

#define MAX_PRODUCTS 100
#define MAX_ORDERS 1000
#define DEFAULT_WORKERS 4
#define MAX_WORKERS 32

typedef struct {
    int id, stock;
    char name[64];
    pthread_mutex_t lock;
} Product;
typedef struct { int id, product_id, quantity, success; } Order;
static Product products[MAX_PRODUCTS];
static Order orders[MAX_ORDERS];
static int np, no, next_order;
static int num_workers = DEFAULT_WORKERS;
static pthread_mutex_t queue_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_lock=PTHREAD_MUTEX_INITIALIZER;
static FILE *log_file = NULL;

static int find_product(int id) { for(int i=0;i<np;i++) if(products[i].id==id) return i; return -1; }
static int read_int(const char *prompt) {
    char line[128], extra;
    int n;
    for(;;) {
        printf("%s",prompt);
        if(!fgets(line,sizeof line,stdin)) return -1;
        if(sscanf(line," %d %c",&n,&extra)==1) return n;
        puts("Please enter a valid number.");
    }
}
static int load(const char *pf,const char *of,int allow_empty) {
    FILE *f=fopen(pf,"r"); if(!f){perror(pf);return 0;}
    np=0;
    while(np<MAX_PRODUCTS && fscanf(f,"%d %63s %d",&products[np].id,products[np].name,&products[np].stock)==3) {
        pthread_mutex_init(&products[np].lock, NULL);
        np++;
    }
    fclose(f);
    f=fopen(of,"r"); if(!f){perror(of);return 0;}
    no=0;
    while(no<MAX_ORDERS && fscanf(f,"%d %d %d",&orders[no].id,&orders[no].product_id,&orders[no].quantity)==3){orders[no].success=0;no++;}
    fclose(f);
    return np>0 && (allow_empty || no>0);
}
static int save_stock(void) {
    const char *tmp="input/custom_products.tmp";
    FILE *f=fopen(tmp,"w"); if(!f){perror(tmp);return 0;}
    for(int i=0;i<np;i++) fprintf(f,"%d %s %d\n",products[i].id,products[i].name,products[i].stock);
    if(fclose(f)!=0 || rename(tmp,"input/custom_products.txt")!=0){perror("save stock");return 0;}
    return 1;
}
static int save_orders(void) {
    FILE *f=fopen("input/custom_orders.txt","w"); if(!f){perror("custom_orders");return 0;}
    for(int i=0;i<no;i++) fprintf(f,"%d %d %d\n",orders[i].id,orders[i].product_id,orders[i].quantity);
    fclose(f);return 1;
}
static void show_products(void) {
    puts("\nID   PRODUCT                  STOCK");
    for(int i=0;i<np;i++) printf("%-4d %-24s %d\n",products[i].id,products[i].name,products[i].stock);
}
static void show_orders(void) {
    printf("\nTotal orders: %d\n",no);
    puts("ORDER_ID PRODUCT_ID QUANTITY");
    for(int i=0;i<no;i++) printf("%-8d %-10d %d\n",orders[i].id,orders[i].product_id,orders[i].quantity);
}
static void add_order(void) {
    if(no>=MAX_ORDERS){puts("Order limit reached.");return;}
    show_products();
    int pid=read_int("Product ID: ");
    if(find_product(pid)<0){puts("Product not found.");return;}
    int qty=read_int("Quantity: ");
    if(qty<=0){puts("Quantity must be positive.");return;}
    puts("Order queued. Stock will be checked when processing.");
    int maxid=0;for(int i=0;i<no;i++) if(orders[i].id>maxid)maxid=orders[i].id;
    orders[no++]=(Order){maxid+1,pid,qty,0};
    if(save_orders()) printf("Added order #%d (product %d, quantity %d).\n",maxid+1,pid,qty);
}
static void adjust_stock(void) {
    show_products();
    int pid=read_int("Product ID: ");
    int p=find_product(pid);
    if(p<0){puts("Product not found.");return;}
    puts("1. Increase stock");
    puts("2. Decrease stock");
    int mode=read_int("Choose: ");
    if(mode!=1&&mode!=2){puts("Invalid choice.");return;}
    int amount=read_int("Amount: ");
    if(amount<=0){puts("Amount must be positive.");return;}
    pthread_mutex_lock(&products[p].lock);
    if(mode==1){
        if(amount>INT_MAX-products[p].stock){
            pthread_mutex_unlock(&products[p].lock);
            puts("Stock value too large.");
            return;
        }
        products[p].stock+=amount;
    }else{
        if(amount>products[p].stock){
            pthread_mutex_unlock(&products[p].lock);
            printf("Cannot decrease by %d. Current stock is only %d.\n",amount,products[p].stock);
            return;
        }
        products[p].stock-=amount;
    }
    pthread_mutex_unlock(&products[p].lock);
    if(save_stock()) printf("Updated %s: new stock is %d.\n",products[p].name,products[p].stock);
    else puts("ERROR: Could not save stock. Check permissions.");
}
static void log_transaction(int worker_id, const Order *o, const char *pname, int success, int rem_stock) {
    if (!log_file) return;
    time_t rawtime;
    struct tm *timeinfo;
    char time_str[32];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);

    pthread_mutex_lock(&log_lock);
    if (log_file) {
        if (worker_id >= 0) {
            fprintf(log_file, "[%s] [Worker %02d] Order #%-4d | Product: %-12s (ID: %d) | Qty: %-3d | Status: %-7s | Remaining: %d\n",
                    time_str, worker_id + 1, o->id, pname, o->product_id, o->quantity,
                    success ? "SUCCESS" : "FAILED", rem_stock);
        } else {
            fprintf(log_file, "[%s] [Sequential] Order #%-4d | Product: %-12s (ID: %d) | Qty: %-3d | Status: %-7s | Remaining: %d\n",
                    time_str, o->id, pname, o->product_id, o->quantity,
                    success ? "SUCCESS" : "FAILED", rem_stock);
        }
        fflush(log_file);
    }
    pthread_mutex_unlock(&log_lock);
}

static void process(int i, int worker_id) {
    Order *o=&orders[i];
    int p=find_product(o->product_id);
    const char *pname = "Unknown";
    int rem_stock = -1;
    if(p>=0 && o->quantity>0){
        pthread_mutex_lock(&products[p].lock);
        pname = products[p].name;
        if(products[p].stock>=o->quantity){
            products[p].stock-=o->quantity;
            o->success=1;
        }else{
            o->success=0;
        }
        rem_stock = products[p].stock;
        pthread_mutex_unlock(&products[p].lock);
    }else{
        o->success=0;
    }
    log_transaction(worker_id, o, pname, o->success, rem_stock);
    struct timespec delay={0,20000000};nanosleep(&delay,NULL);
}
static void *worker(void *arg) {
    int wid = (int)(intptr_t)arg;
    for(;;){
        pthread_mutex_lock(&queue_lock);
        int i=next_order++;
        pthread_mutex_unlock(&queue_lock);
        if(i>=no)break;
        process(i, wid);
    }
    return NULL;
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void run_loaded(const char *label,const char *outfile,int parallel) {
    if(no==0){puts("No orders to process.");return;}
    next_order=0;
    for(int i=0;i<no;i++)orders[i].success=0;

    make_dir("output");
    pthread_mutex_lock(&log_lock);
    log_file = fopen("output/transactions.log", "a");
    if (log_file) {
        fprintf(log_file, "\n--- SESSION: %s | Mode: %s (%d workers) ---\n",
                label, parallel ? "MULTITHREAD" : "SEQUENTIAL", parallel ? num_workers : 1);
    }
    pthread_mutex_unlock(&log_lock);

    double start=now();
    if(parallel){
        int nthreads = (num_workers > 0 && num_workers <= MAX_WORKERS) ? num_workers : DEFAULT_WORKERS;
        pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * nthreads);
        int created=0;
        for(int i=0;i<nthreads;i++){
            if(pthread_create(&threads[i],NULL,worker,(void *)(intptr_t)i)!=0){puts("Thread creation failed.");break;}
            created++;
        }
        if(created==0){for(int i=0;i<no;i++)process(i, -1);}
        for(int i=0;i<created;i++)pthread_join(threads[i],NULL);
        free(threads);
    }else for(int i=0;i<no;i++)process(i, -1);
    double elapsed=now()-start;
    int ok=0;for(int i=0;i<no;i++)ok+=orders[i].success;

    pthread_mutex_lock(&log_lock);
    if (log_file) {
        fprintf(log_file, "--- END SESSION: Orders: %d | Success: %d | Failed: %d | Time: %.4fs ---\n",
                no, ok, no-ok, elapsed);
        fclose(log_file);
        log_file = NULL;
    }
    pthread_mutex_unlock(&log_lock);

    // Sales and inventory analytics
    int total_units_sold = 0;
    int top_idx = -1;
    int max_sold = -1;
    for(int i=0; i<np; i++) {
        int sold = 0;
        for(int j=0; j<no; j++) {
            if(orders[j].product_id == products[i].id && orders[j].success) {
                sold += orders[j].quantity;
            }
        }
        total_units_sold += sold;
        if(sold > max_sold && sold > 0) {
            max_sold = sold;
            top_idx = i;
        }
    }
    double fulfillment_rate = (no > 0) ? ((double)ok / no * 100.0) : 0.0;

    FILE *f=fopen(outfile,"w");
    if(f){
        fprintf(f,"Scenario: %s | Mode: %s\n",label,parallel?"MULTITHREAD":"SEQUENTIAL");
        if(parallel) fprintf(f,"Worker Threads: %d\n",num_workers);
        fprintf(f,"Orders: %d | Success: %d | Failed: %d | Time: %.4f seconds\n\n",no,ok,no-ok,elapsed);
        fprintf(f,"ORDER_ID PRODUCT_ID QUANTITY STATUS\n");
        for(int i=0;i<no;i++)fprintf(f,"%d %d %d %s\n",orders[i].id,orders[i].product_id,orders[i].quantity,orders[i].success?"SUCCESS":"FAILED");
        fprintf(f,"\nREMAINING STOCK\n");
        for(int i=0;i<np;i++)fprintf(f,"%d %s %d\n",products[i].id,products[i].name,products[i].stock);
        fprintf(f,"\n--- SALES & INVENTORY ANALYTICS ---\n");
        fprintf(f,"Fulfillment Rate: %.2f%% (%d/%d orders)\n", fulfillment_rate, ok, no);
        fprintf(f,"Total Units Sold: %d items\n", total_units_sold);
        if(top_idx >= 0)
            fprintf(f,"Top Selling Product: %s (ID: %d) with %d units sold\n", products[top_idx].name, products[top_idx].id, max_sold);
        else
            fprintf(f,"Top Selling Product: None (no units sold)\n");
        fclose(f);
    }else perror(outfile);
    if(parallel)
        printf("\n%s | MULTITHREAD (%d workers) | Orders: %d | Success: %d | Failed: %d | Time: %.4f s\n",label,num_workers,no,ok,no-ok,elapsed);
    else
        printf("\n%s | SEQUENTIAL | Orders: %d | Success: %d | Failed: %d | Time: %.4f s\n",label,no,ok,no-ok,elapsed);
    printf("Analytics: Fulfillment: %.1f%% | Units Sold: %d", fulfillment_rate, total_units_sold);
    if(top_idx >= 0)
        printf(" | Best Seller: %s (%d items)\n", products[top_idx].name, max_sold);
    else
        printf("\n");
    printf("Result saved: %s\n",outfile);
    show_products();
}
static void custom_run(int parallel) {
    if(!load("input/custom_products.txt","input/custom_orders.txt",1)){puts("Cannot load custom data.");return;}
    if(no==0){puts("No pending orders.");return;}
    run_loaded("CUSTOM",parallel?"output/custom_parallel.txt":"output/custom_sequential.txt",parallel);
    if(!save_stock()){puts("ERROR: Could not save stock. Check permissions.");return;}
    // Orders are consumed once; successful orders reduce stock permanently.
    // Failed orders are logged in the result file and can be resubmitted manually.
    no=0;
    if(!save_orders())puts("ERROR: Could not clear processed orders.");
    else puts("Stock saved permanently. Processed orders cleared from pending list.");
}
static void scenario(int s) {
    char pf[80],of[80],out[100];
    snprintf(pf,sizeof pf,"input/products%d.txt",s);
    snprintf(of,sizeof of,"input/orders%d.txt",s);
    for(int parallel=0;parallel<=1;parallel++){
        if(!load(pf,of,0)){puts("Cannot load scenario data.");return;}
        snprintf(out,sizeof out,"output/scenario%d_%s.txt",s,parallel?"parallel":"sequential");
        char label[32];snprintf(label,sizeof label,"Scenario %d",s);
        run_loaded(label,out,parallel);
    }
}
static void view_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("\nFile not found or not yet generated: %s\n", path);
        return;
    }
    printf("\n=== CONTENTS OF %s ===\n", path);
    char buf[256];
    while (fgets(buf, sizeof buf, f)) {
        fputs(buf, stdout);
    }
    printf("=== END OF FILE ===\n\n");
    fclose(f);
}

static void view_results(void) {
    puts("\n--- View Result Files ---");
    puts("1. Custom - Sequential (output/custom_sequential.txt)");
    puts("2. Custom - Multithread (output/custom_parallel.txt)");
    puts("3. Scenario 1 - Sequential (output/scenario1_sequential.txt)");
    puts("4. Scenario 1 - Parallel (output/scenario1_parallel.txt)");
    puts("5. Scenario 2 - Sequential (output/scenario2_sequential.txt)");
    puts("6. Scenario 2 - Parallel (output/scenario2_parallel.txt)");
    puts("7. Scenario 3 - Sequential (output/scenario3_sequential.txt)");
    puts("8. Scenario 3 - Parallel (output/scenario3_parallel.txt)");
    puts("9. Full Transaction Log (output/transactions.log)");
    puts("0. Back to Main Menu");
    int ch = read_int("Choose file to view: ");
    const char *files[] = {
        NULL,
        "output/custom_sequential.txt",
        "output/custom_parallel.txt",
        "output/scenario1_sequential.txt",
        "output/scenario1_parallel.txt",
        "output/scenario2_sequential.txt",
        "output/scenario2_parallel.txt",
        "output/scenario3_sequential.txt",
        "output/scenario3_parallel.txt",
        "output/transactions.log"
    };
    if (ch >= 1 && ch <= 9) {
        view_file(files[ch]);
    }
}

static void set_worker_threads(void) {
    printf("\nCurrent worker threads: %d\n", num_workers);
    int n = read_int("Enter new number of worker threads (1-32): ");
    if (n < 1 || n > MAX_WORKERS) {
        printf("Invalid choice. Number of workers must be between 1 and %d.\n", MAX_WORKERS);
        return;
    }
    num_workers = n;
    printf("Worker threads updated to %d.\n", num_workers);
}

static void run_benchmark(int s) {
    char pf[80], of[80];
    snprintf(pf, sizeof pf, "input/products%d.txt", s);
    snprintf(of, sizeof of, "input/orders%d.txt", s);

    printf("\n=============================================================================\n");
    printf("                  MULTI-THREADED PERFORMANCE BENCHMARK\n");
    printf("                    Scenario %d | High-Load Testing\n", s);
    printf("=============================================================================\n");
    printf(" %-12s  %-8s  %-12s  %-10s  %-10s  %-12s\n",
           "Mode", "Workers", "Time (s)", "Speedup", "Efficiency", "Orders (OK/All)");
    printf("-----------------------------------------------------------------------------\n");

    // 1. Run Sequential
    if (!load(pf, of, 0)) { puts("Cannot load scenario data."); return; }
    next_order = 0;
    for (int i = 0; i < no; i++) orders[i].success = 0;
    double t_start = now();
    for (int i = 0; i < no; i++) process(i, -1);
    double t_seq = now() - t_start;
    int ok_seq = 0;
    for (int i = 0; i < no; i++) ok_seq += orders[i].success;

    char tseq_buf[16], okseq_buf[16];
    snprintf(tseq_buf, sizeof(tseq_buf), "%.4f s", t_seq);
    snprintf(okseq_buf, sizeof(okseq_buf), "%d/%d", ok_seq, no);
    printf(" %-12s  %-8d  %-12s  %-10s  %-11s  %-12s\n",
           "Sequential", 1, tseq_buf, "1.00x", "100.0%", okseq_buf);

    // 2. Run Parallel with 1, 2, 4, 8, 16 threads
    int test_workers[] = {1, 2, 4, 8, 16};
    int num_tests = sizeof(test_workers) / sizeof(test_workers[0]);

    for (int t = 0; t < num_tests; t++) {
        int w = test_workers[t];
        if (!load(pf, of, 0)) continue;
        next_order = 0;
        for (int i = 0; i < no; i++) orders[i].success = 0;

        t_start = now();
        pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * w);
        int created = 0;
        for (int i = 0; i < w; i++) {
            if (pthread_create(&threads[i], NULL, worker, (void *)(intptr_t)i) == 0) created++;
        }
        for (int i = 0; i < created; i++) pthread_join(threads[i], NULL);
        free(threads);
        double t_par = now() - t_start;

        int ok_par = 0;
        for (int i = 0; i < no; i++) ok_par += orders[i].success;

        double speedup = (t_par > 0) ? (t_seq / t_par) : 0.0;
        double efficiency = (w > 0) ? ((speedup / w) * 100.0) : 0.0;

        char tpar_buf[16], spd_buf[16], eff_buf[16], okpar_buf[16];
        snprintf(tpar_buf, sizeof(tpar_buf), "%.4f s", t_par);
        snprintf(spd_buf, sizeof(spd_buf), "%.2fx", speedup);
        snprintf(eff_buf, sizeof(eff_buf), "%.1f%%", efficiency);
        snprintf(okpar_buf, sizeof(okpar_buf), "%d/%d", ok_par, no);

        printf(" %-12s  %-8d  %-12s  %-10s  %-11s  %-12s\n",
               "Multithread", w, tpar_buf, spd_buf, eff_buf, okpar_buf);
    }
    printf("=============================================================================\n\n");
}

int main(int argc, char **argv) {
    make_dir("input");
    make_dir("output");

    if (argc > 1) {
        if (strcmp(argv[1], "--benchmark") == 0 || strcmp(argv[1], "-b") == 0) {
            int sc = 3;
            if (argc > 2) sc = atoi(argv[2]);
            if (sc < 1 || sc > 3) sc = 3;
            run_benchmark(sc);
            return 0;
        } else if (strcmp(argv[1], "--test") == 0 || strcmp(argv[1], "-t") == 0) {
            scenario(1);
            return 0;
        } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            puts("Usage: ./main [OPTIONS]");
            puts("Options:");
            puts("  -b, --benchmark [1|2|3]   Run multi-threaded benchmark (default scenario: 3)");
            puts("  -t, --test                Run basic test (Scenario 1)");
            puts("  -h, --help                Show this help message");
            puts("  (no args)                 Start interactive menu");
            return 0;
        }
    }

    if(!load("input/custom_products.txt","input/custom_orders.txt",1)){
        puts("Custom data files are missing. Check input directory.");return 1;
    }
    for(;;){
        puts("\n===== E-COMMERCE ORDER SYSTEM V4 =====");
        puts("1. View Products");
        puts("2. Add New Order");
        puts("3. View Pending Orders");
        puts("4. Process Custom Orders - Sequential");
        puts("5. Process Custom Orders - Multithread");
        puts("6. View Results Content");
        puts("7. Run 3 Test Scenarios (does not change custom stock)");
        puts("8. Adjust Product Stock (increase/decrease)");
        printf("9. Configure Worker Threads (current: %d)\n", num_workers);
        puts("10. Run Automated Threading Benchmark (Amdahl's Law)");
        puts("0. Exit");
        int c=read_int("Choose: ");
        if(c==0 || c==-1)break;
        if(c==1){load("input/custom_products.txt","input/custom_orders.txt",1);show_products();}
        else if(c==2){load("input/custom_products.txt","input/custom_orders.txt",1);add_order();}
        else if(c==3){load("input/custom_products.txt","input/custom_orders.txt",1);show_orders();}
        else if(c==4)custom_run(0);
        else if(c==5)custom_run(1);
        else if(c==6)view_results();
        else if(c==7){int s=read_int("Scenario (1=Basic, 2=Stock contention, 3=High load): ");if(s>=1&&s<=3)scenario(s);else puts("Invalid scenario.");}
        else if(c==8){load("input/custom_products.txt","input/custom_orders.txt",1);adjust_stock();}
        else if(c==9)set_worker_threads();
        else if(c==10){
            int s=read_int("Benchmark Scenario (1=Basic, 2=Stock contention, 3=High load [default]): ");
            if(s<1||s>3) s=3;
            run_benchmark(s);
        }
        else puts("Invalid choice.");
    }
    return 0;
}

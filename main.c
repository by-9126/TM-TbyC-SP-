#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <limits.h>

#define MAX_PRODUCTS 100
#define MAX_ORDERS 1000
#define WORKERS 4

typedef struct { int id, stock; char name[64]; } Product;
typedef struct { int id, product_id, quantity, success; } Order;
static Product products[MAX_PRODUCTS];
static Order orders[MAX_ORDERS];
static int np, no, next_order;
static pthread_mutex_t queue_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t stock_lock=PTHREAD_MUTEX_INITIALIZER;

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
    while(np<MAX_PRODUCTS && fscanf(f,"%d %63s %d",&products[np].id,products[np].name,&products[np].stock)==3) np++;
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
    if(mode==1){
        if(amount>INT_MAX-products[p].stock){puts("Stock value too large.");return;}
        products[p].stock+=amount;
    }else{
        if(amount>products[p].stock){
            printf("Cannot decrease by %d. Current stock is only %d.\n",amount,products[p].stock);
            return;
        }
        products[p].stock-=amount;
    }
    if(save_stock()) printf("Updated %s: new stock is %d.\n",products[p].name,products[p].stock);
    else puts("ERROR: Could not save stock. Check permissions.");
}
static void process(int i) {
    Order *o=&orders[i];
    pthread_mutex_lock(&stock_lock);
    int p=find_product(o->product_id);
    if(p>=0 && o->quantity>0 && products[p].stock>=o->quantity){products[p].stock-=o->quantity;o->success=1;}
    else o->success=0;
    pthread_mutex_unlock(&stock_lock);
    struct timespec delay={0,20000000};nanosleep(&delay,NULL);
}
static void *worker(void *arg) {
    (void)arg;
    for(;;){
        pthread_mutex_lock(&queue_lock);
        int i=next_order++;
        pthread_mutex_unlock(&queue_lock);
        if(i>=no)break;
        process(i);
    }
    return NULL;
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void run_loaded(const char *label,const char *outfile,int parallel) {
    if(no==0){puts("No orders to process.");return;}
    next_order=0;
    for(int i=0;i<no;i++)orders[i].success=0;
    double start=now();
    if(parallel){
        pthread_t threads[WORKERS];int created=0;
        for(int i=0;i<WORKERS;i++){
            if(pthread_create(&threads[i],NULL,worker,NULL)!=0){puts("Thread creation failed.");break;}
            created++;
        }
        if(created==0){for(int i=0;i<no;i++)process(i);}
        for(int i=0;i<created;i++)pthread_join(threads[i],NULL);
    }else for(int i=0;i<no;i++)process(i);
    double elapsed=now()-start;
    int ok=0;for(int i=0;i<no;i++)ok+=orders[i].success;
    FILE *f=fopen(outfile,"w");
    if(f){
        fprintf(f,"Scenario: %s | Mode: %s\n",label,parallel?"MULTITHREAD":"SEQUENTIAL");
        fprintf(f,"Orders: %d | Success: %d | Failed: %d | Time: %.4f seconds\n\n",no,ok,no-ok,elapsed);
        fprintf(f,"ORDER_ID PRODUCT_ID QUANTITY STATUS\n");
        for(int i=0;i<no;i++)fprintf(f,"%d %d %d %s\n",orders[i].id,orders[i].product_id,orders[i].quantity,orders[i].success?"SUCCESS":"FAILED");
        fprintf(f,"\nREMAINING STOCK\n");
        for(int i=0;i<np;i++)fprintf(f,"%d %s %d\n",products[i].id,products[i].name,products[i].stock);
        fclose(f);
    }else perror(outfile);
    printf("\n%s | %s | Orders: %d | Success: %d | Failed: %d | Time: %.4f s\n",label,parallel?"MULTITHREAD":"SEQUENTIAL",no,ok,no-ok,elapsed);
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
int main(void) {
    if(!load("input/custom_products.txt","input/custom_orders.txt",1)){
        puts("Custom data files are missing. Check input directory.");return 1;
    }
    for(;;){
        puts("\n===== E-COMMERCE ORDER SYSTEM V3 =====");
        puts("1. View Products");
        puts("2. Add New Order");
        puts("3. View Pending Orders");
        puts("4. Process Custom Orders - Sequential");
        puts("5. Process Custom Orders - Multithread");
        puts("6. View Results (file paths)");
        puts("7. Run 3 Test Scenarios (does not change custom stock)");
        puts("8. Adjust Product Stock (increase/decrease)");
        puts("0. Exit");
        int c=read_int("Choose: ");
        if(c==0 || c==-1)break;
        if(c==1){load("input/custom_products.txt","input/custom_orders.txt",1);show_products();}
        else if(c==2){load("input/custom_products.txt","input/custom_orders.txt",1);add_order();}
        else if(c==3){load("input/custom_products.txt","input/custom_orders.txt",1);show_orders();}
        else if(c==4)custom_run(0);
        else if(c==5)custom_run(1);
        else if(c==6)puts("Open: output/custom_sequential.txt, output/custom_parallel.txt, or output/scenarioN_*.txt (use cat filename)");
        else if(c==7){int s=read_int("Scenario (1=Basic, 2=Stock contention, 3=High load): ");if(s>=1&&s<=3)scenario(s);else puts("Invalid scenario.");}
        else if(c==8){load("input/custom_products.txt","input/custom_orders.txt",1);adjust_stock();}
        else puts("Invalid choice.");
    }
    return 0;
}

import org.apache.spark.SparkConf;
import org.apache.spark.api.java.JavaRDD;
import org.apache.spark.api.java.JavaSparkContext;
import org.apache.spark.broadcast.Broadcast;

import org.HdrHistogram.ConcurrentHistogram;

import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicLong;


public class test {
    public static final int write_num = 4;
    public static AtomicLong times1 = new AtomicLong(0);
    public static AtomicLong times2 = new AtomicLong(0);
    public static AtomicLong times3 = new AtomicLong(0);

    public static ConcurrentHistogram histogram = new ConcurrentHistogram(10,10000000,3);

    public static Vector<byte []> file_buffer;
    public static int create_file_size = 1;
    public static int batch_size = 2000;
    public static byte[] target = new byte[4096];
    public static void main(String[] args) {

        SparkConf conf = new SparkConf().setAppName("SparkApp").setMaster("spark://10.0.0.2:7077").set("spark.driver.host", "10.0.0.2");
        JavaSparkContext sc = new JavaSparkContext(conf);
        sc.setLogLevel("ERROR");

        file_buffer = new Vector<>();
        for (int i = 0; i < create_file_size; i++) {
            file_buffer.add(new byte[32768]);
        }


        ExecutorService es = Executors.newFixedThreadPool(create_file_size);
//        ExecutorService es = Executors.newSingleThreadExecutor();

        long time_begin = System.nanoTime();
        for(int i=0; i< create_file_size*batch_size; i++) {
            es.execute(new Task1(sc, i));
        }
        es.shutdown();
        try {
            while(!es.awaitTermination(1, TimeUnit.SECONDS)) {
            }
        } catch (InterruptedException e) {
            e.printStackTrace();
        }



        times3.addAndGet(System.nanoTime() - time_begin);

        System.out.printf("%f %f %f\n", times1.get()/1e9, times2.get()/1e9, create_file_size*batch_size/(times3.get()/1e9));
        sc.stop();

        histogram.outputPercentileDistribution(System.out, 5,1.0, false);
    }
}

class Task1 implements Runnable {
    private final int index;
    private final JavaSparkContext sc;
    public Task1(JavaSparkContext sc, int index) {
        this.index = index;
        this.sc = sc;
    }

    @Override
    public void run() {
//        System.out.println("begin "+ index);
        long time1 = System.nanoTime();
        Broadcast<byte[]> broadcastData = sc.broadcast(test.file_buffer.get(index % test.create_file_size));
        long time2 = System.nanoTime();
        sc.parallelize(new ArrayList<Integer>(), 1).foreach(x -> {
            byte []new_data = broadcastData.value();
            for(int w = 0; w< test.write_num; w++){
                System.arraycopy(test.target,0, new_data,w*4096, 4096);
            }
        });
        long time3 = System.nanoTime();
        test.times1.addAndGet(time2 - time1);
        test.times2.addAndGet(time3 - time2);

        test.histogram.recordValue((time3-time1)/1000);

        broadcastData.destroy(true);
//        System.out.println("end "+ index);
    }
}

class Task2 implements Runnable {
    private final int index;
    private final JavaSparkContext sc;
    public Task2(JavaSparkContext sc, int index) {
        this.index = index;
        this.sc = sc;
    }

    @Override
    public void run() {
//        System.out.println("begin "+ index);
        long time1 = System.nanoTime();

        JavaRDD<byte[]> rdd = sc.parallelize(Arrays.asList(test.file_buffer.get(index % test.create_file_size)), 1);
        long time2 = System.nanoTime();

        rdd.foreach(bytes -> {

        });

        long time3 = System.nanoTime();
        test.times1.addAndGet(time2 - time1);
        test.times2.addAndGet(time3 - time2);

        test.histogram.recordValue((time3-time1)/1000);

//        System.out.println("end "+ index + "  "+ (time3-time1)/100);
    }
}

//        // create two RDDs, one for each task
//        JavaRDD<String> rdd1 = jsc.parallelize(Arrays.asList("hello from task 1"));
//        JavaRDD<String> rdd2 = jsc.parallelize(Arrays.asList("hello from task 2"));
//
//        // assign each task to a specific worker
//        String worker2 = "192.168.189.7:7077";
//        String worker1 = "192.168.189.9:7077";
//
////        jsc.setLocalProperty("spark.scheduler.pool", "pool1");
////        jsc.setLocalProperty("spark.executor.hostname", worker1);
//        rdd1.foreach(x -> System.out.println(x));
//
////        jsc.setLocalProperty("spark.scheduler.pool", "pool2");
////        jsc.setLocalProperty("spark.executor.hostname", worker2);
//        rdd2.foreach(x -> System.out.println(x));
//        JavaRDD<String> data = sc.parallelize(
//                Arrays.asList("Hello", "World", "Spark", "Java"), 1
//        );
//        data.persist(org.apache.spark.storage.StorageLevel.MEMORY_ONLY());
//
//        org.apache.spark.rdd.RDD<String> rdd = data.rdd();
//
//        System.out.println("rdd id" + rdd.id());
//
//        Map<Integer, JavaRDD<?>> persistentRDDS = sc.getPersistentRDDs();
//        for (Map.Entry<Integer, JavaRDD<?>> entry : persistentRDDS.entrySet()) {
//            int rddId = entry.getKey();
//            JavaRDD<?> javaRDD = entry.getValue();
//            javaRDD.foreach(x -> System.out.println(x));
//            System.out.println("RDD ID: " + rddId);
//            System.out.println("Storage Level: " + javaRDD.getStorageLevel());
//            System.out.println("Partitions: " + rdd.getNumPartitions());
//            System.out.println("Memory Size: " + rdd.count());
//        }
//
//        // 打印RDD中的所有元素
//        System.out.println("hello world");
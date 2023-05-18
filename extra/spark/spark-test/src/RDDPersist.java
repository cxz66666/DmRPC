import org.apache.spark.SparkConf;
import org.apache.spark.api.java.JavaRDD;
import org.apache.spark.api.java.JavaSparkContext;

import java.util.Arrays;
import java.util.Map;

public class RDDPersist {
    public static void main(String[] args) {
        SparkConf conf = new SparkConf().setAppName("Spark Hello World").setMaster("spark://192.168.189.8:7077");

        // 创建JavaSparkContext对象，参数为SparkConf对象
        JavaSparkContext sc = new JavaSparkContext(conf);

        // 创建一个包含字符串的JavaRDD
        JavaRDD<String> data = sc.parallelize(
                Arrays.asList("Hello", "World", "Spark", "Java")
        );

        data.persist(org.apache.spark.storage.StorageLevel.MEMORY_ONLY());

        org.apache.spark.rdd.RDD<String> rdd = data.rdd();

        System.out.println(rdd.toDebugString());


        Map<Integer, JavaRDD<?>> persistentRDDS = sc.getPersistentRDDs();
        for (Map.Entry<Integer, JavaRDD<?>> entry : persistentRDDS.entrySet()) {
            int rddId = entry.getKey();
            JavaRDD<?> javaRDD = entry.getValue();
            System.out.println("RDD ID: " + rddId);
            System.out.println("Storage Level: " + javaRDD.getStorageLevel());
            System.out.println("Partitions: " + rdd.getNumPartitions());
            System.out.println("Memory Size: " + rdd.count());
        }

        // 打印RDD中的所有元素
        System.out.println("hello world");
        // 关闭JavaSparkContext对象
        sc.stop();
    }
}
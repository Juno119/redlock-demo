package main

import (
	"context"
	"flag"
	"fmt"
	goredislib "github.com/go-redis/redis/v8"
	redsync "github.com/go-redsync/redsync/v4"
	goredis "github.com/go-redsync/redsync/v4/redis/goredis/v8"
	"net/http"
	"strconv"
)

var rs *redsync.Redsync
var client *goredislib.Client
var stopFlag bool

func HelloServer(w http.ResponseWriter, r *http.Request) {
	//fmt.Fprintf(w, "Hello, %s!", r.URL.Path[2:])

	query := r.URL.Query()
	name := query.Get("name") //filters="color"
	fmt.Fprintf(w, "Hello, %s!", name)

	if stopFlag {
		fmt.Println("ticket sold out")
		return
	}
	// Obtain a new mutex by using the same name for all instances wanting the
	// same lock.
	mutexname := "lock"
	mutex := rs.NewMutex(mutexname)

	// Obtain a lock for our given mutex. After this is successful, no one else
	// can obtain the same lock (the same mutex name) until we unlock it.
	if err := mutex.Lock(); err != nil {
		//panic(err)
		//fmt.Println("Get lock err:", err)
		return
	}
	key := "ticket"
	ctx := context.Background()
	// Do your work that requires the lock.
	ticket, err := client.Get(ctx, key).Result()
	if err != nil {
		//panic(err)
		//fmt.Println("Get key err:", err)
		return
	}
	fmt.Println("get ticket:", ticket)

	if val, _ := strconv.Atoi(ticket); val > 0 {
		err := client.Decr(ctx, key).Err()
		if err != nil {
			//panic(err)
			fmt.Println("Decr key err:", err)
			return
		}
	} else {
		stopFlag = true
		fmt.Println("ticket sold out ticket=", ticket)
	}

	defer func() {
		// Release the lock so other processes or threads can obtain a lock.
		if ok, err := mutex.Unlock(); !ok || err != nil {
			//panic("unlock failed")
			fmt.Println("unlock failed err:", err)
			return
		}
	}()
}

func main() {
	// Create a pool with go-redis (or redigo) which is the pool redisync will
	// use while communicating with Redis. This can also be any pool that
	// implements the `redis.Pool` interface.
	client = goredislib.NewClient(&goredislib.Options{
		Addr:     "sh-crs-9yng0a0i.sql.tencentcdb.com:25817",
		Password: "redis12#$",
		DB:       2, // use default DB
	})
	key := "ticket"
	ctx := context.Background()
	ticket, err := client.Get(ctx, key).Result()
	if err != nil {
		panic(err)
	}
	fmt.Println("init ticket:", ticket)
	pool := goredis.NewPool(client) // or, pool := redigo.NewPool(...)

	// Create an instance of redisync to be used to obtain a mutual exclusion
	// lock.
	rs = redsync.New(pool)
	host := flag.String("host", "0.0.0.0", "host")
	port := flag.Int("port", 8080, "port")
	flag.Parse()
	http.HandleFunc("/go/Hello", HelloServer)
	fmt.Println("addr=", fmt.Sprintf("%s:%d", *host, *port))
	http.ListenAndServe(fmt.Sprintf("%s:%d", *host, *port), nil)
}

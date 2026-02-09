package main

import (
	"context"
	"encoding/binary"
	"fmt"
	"log"
	"log/slog"
	"math/rand"
	"os"
	"os/signal"
	"strconv"
	"sync"
	"syscall"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	influxdb2 "github.com/influxdata/influxdb-client-go/v2"
	"github.com/influxdata/influxdb-client-go/v2/api"
	"github.com/joho/godotenv"
)

type Config struct {
	BrokerURL    string
	Topic        string
	WorkerCount  int
	QueueSize    int
	InfluxURL    string
	InfluxToken  string
	InfluxOrg    string
	InfluxBucket string
}

func LoadConfig() Config {
	return Config{
		BrokerURL:    getEnv("BROKER_URL", "tcp://localhost:1883"),
		Topic:        getEnv("TOPIC", "fleet/+/telemetry"),
		WorkerCount:  getEnvInt("WORKER_COUNT", 100),
		QueueSize:    getEnvInt("QUEUE_SIZE", 2000),
		InfluxURL:    getEnv("INFLUX_URL", "http://localhost:8086"),
		InfluxToken:  getEnv("INFLUX_TOKEN", ""),
		InfluxOrg:    getEnv("INFLUX_ORG", "DesmoTelemetry"),
		InfluxBucket: getEnv("INFLUX_BUCKET", "Telemetry"),
	}
}

type TelemetryPacket struct {
	Magic        uint16
	VehicleId    uint16
	SequenceId   uint32
	Timestamp    uint64
	RPM          uint16
	Speed        uint16
	Jerk         int16
	Temp         uint8
	BatteryLevel uint8
	Gear         uint8
	Flags        uint8
	Version      uint8
	CPULoad      uint8
	CRC16        uint16
}

type Job struct {
	Topic   string
	Payload []byte
}

// WorkerPool to avoid blocking the network. Enforce read-only safety
func startWorkerPool(ctx context.Context, cfg Config, jobs <-chan Job, wg *sync.WaitGroup, writeAPI api.WriteAPI) {
	for i := 0; i < cfg.WorkerCount; i++ {
		wg.Add(1)
		// Start a new goroutine
		go func(workerId int) {
			defer wg.Done() // Signal when this worker stops.
			logger := slog.With("worker", workerId)

			for {
				select {
				case <-ctx.Done():
					return
				case job, ok := <-jobs:
					if !ok {
						return
					}
					processJob(logger, job, writeAPI)
				}
			}
		}(i)
	}
}

func processJob(logger *slog.Logger, job Job, writeAPI api.WriteAPI) {
	if len(job.Payload) != 32 {
		logger.Warn("Dropped Invaild Packet, ", "size:", len(job.Payload))
		return
	}

	data := job.Payload
	packet := TelemetryPacket{
		Magic:        binary.BigEndian.Uint16(data[0:2]),
		VehicleId:    binary.BigEndian.Uint16(data[2:4]),
		SequenceId:   binary.BigEndian.Uint32(data[4:8]),
		Timestamp:    binary.BigEndian.Uint64(data[8:16]),
		RPM:          binary.BigEndian.Uint16(data[16:18]),
		Speed:        binary.BigEndian.Uint16(data[18:20]),
		Jerk:         int16(binary.BigEndian.Uint16(data[20:22])),
		Temp:         data[22],
		BatteryLevel: data[23],
		Gear:         data[24],
		Flags:        data[25],
		Version:      data[26],
		CPULoad:      data[27],
		CRC16:        binary.BigEndian.Uint16(data[28:30]),
	}

	p := influxdb2.NewPointWithMeasurement("vehicle_status").
		AddTag("vehicle_id", strconv.Itoa(int(packet.VehicleId))).
		AddField("speed", packet.Speed).
		AddField("rpm", packet.RPM).
		AddField("jerk", packet.Jerk).
		AddField("temp", packet.Temp).
		AddField("battery", packet.BatteryLevel).
		AddField("gear", packet.Gear).
		AddField("flags", packet.Flags).
		SetTime(time.Now())

	// Non-blocking write (Batched in the background)
	writeAPI.WritePoint(p)

	if packet.Flags > 0 {
		logger.Info("Vehicle Alert", "id", packet.VehicleId, "flags", fmt.Sprintf("0x%X", packet.Flags), "jerk", packet.Jerk, "ver", packet.Version)
	}
}

func main() {
	err := godotenv.Load()
	if err != nil {
		slog.Warn("No .env found. Using system environment variables")
	}

	logger := slog.New(slog.NewJSONHandler(os.Stdout, nil))
	slog.SetDefault(logger)
	cfg := LoadConfig()

	// Context
	ctx, cancel := context.WithCancel(context.Background())
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	// Create Influxdb client
	influxClient := influxdb2.NewClient(cfg.InfluxURL, cfg.InfluxToken)
	// Non-blocking write client
	writeAPI := influxClient.WriteAPI(cfg.InfluxOrg, cfg.InfluxBucket)

	go func() {
		for err := range writeAPI.Errors() {
			slog.Error("InfluxDB Write Error", "error", err)
		}
	}()

	// Start Workers
	jobQueue := make(chan Job, cfg.QueueSize)
	var wg sync.WaitGroup
	startWorkerPool(ctx, cfg, jobQueue, &wg, writeAPI)

	// Set up MQTT
	opts := mqtt.NewClientOptions()
	opts.AddBroker(cfg.BrokerURL)

	// Preventing client id collisions
	randID := rand.New(rand.NewSource(time.Now().UnixNano())).Intn(10000)
	opts.SetClientID(fmt.Sprintf("go_ingestor_%d", randID))

	// Prevent queuing of old messages. Start fresh with real-time data
	opts.SetCleanSession(true)

	// Throughput optimization
	opts.SetOrderMatters(false)

	opts.SetOnConnectHandler(func(c mqtt.Client) {
		logger.Info("Connected to Broker", "client_id", opts.ClientID)
		if token := c.Subscribe(cfg.Topic, 1, nil); token.Wait() && token.Error() != nil {
			logger.Error("Resubscribe failed", "error", token.Error())
		}
	})

	opts.SetDefaultPublishHandler(func(c mqtt.Client, msg mqtt.Message) {
		select {
		case jobQueue <- Job{Topic: msg.Topic(), Payload: msg.Payload()}:
		default:
			logger.Warn("Backpressure: Dropping Packet: ", "topic: ", msg.Topic())
		}
	})

	log.Println("-------------------- Desmo Ingestor ------------------------")
	log.Println("Connecting to Mosquitto Broker")

	client := mqtt.NewClient(opts)
	if token := client.Connect(); token.Wait() && token.Error() != nil {
		log.Fatal("Connect failed: %v", token.Error())
	}
	log.Println("Connected to Mosquitto!!")

	<-sigChan
	logger.Info("Terminate signal received")
	logger.Info("\n Shutting down...")
	cancel() // Stop workers
	client.Disconnect(250)

	// Flush remaining database writes before exit
	writeAPI.Flush()
	influxClient.Close()

	wg.Wait() // Drain Worker pool
	logger.Info("Offline.")
}

func getEnv(key, fallback string) string {
	if v, exists := os.LookupEnv(key); exists {
		return v
	}
	return fallback
}

func getEnvInt(key string, fallback int) int {
	if v, exists := os.LookupEnv(key); exists {
		if i, err := strconv.Atoi(v); err == nil {
			return i
		}
	}
	return fallback
}

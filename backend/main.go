package main

import (
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"
	// "time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
)

// 1. Triggers every time a packet arrives
// Separate Goroutine
var messageHandler mqtt.MessageHandler = func(client mqtt.Client, msg mqtt.Message){
	fmt.Printf("[%s] Received %d bytes\n", msg.Topic(), len(msg.Payload()))

	// TODO: DECODE THE BINARY BLOB
}

func main() {
	opts := mqtt.NewClientOptions()
	opts.AddBroker("tcp://localhost:1883")
	opts.SetClientID("go_ingestor")
	opts.SetCleanSession(true)

	log.Println("-------------------- Desmo Ingestor ------------------------")
	log.Println("Connecting to Mosquitto Broker")

	client := mqtt.NewClient(opts)
	if token:=client.Connect(); token.Wait() && token.Error()!=nil{
		log.Fatal("Connect failed: %v", token.Error());
	}
	log.Println("Connected to Mosquitto!!")

	topic := "fleet/+/telemetry"
	if token:= client.Subscribe(topic,1,messageHandler); token.Wait() && token.Error()!=nil {
		log.Fatalf("Error subscribing: %v", token.Error())
	}
	log.Println("Waiting for traffic...")

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt, syscall.SIGTERM)
	<-sig
	log.Println("\n Shutting down...")
	client.Disconnect(250)
	log.Println("Offline.")
}

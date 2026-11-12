package main

import (
	"fmt"
	browser "github.com/TitanCSA/Browser-Control-SDK/Browser-Control-SDK-Go-modules"
	"log"
)

func main() {
	client, err := browser.Connect("")
	if err != nil {
		log.Fatal(err)
	}
	defer client.Close()
	info, err := client.Info()
	if err != nil {
		log.Fatal(err)
	}
	fmt.Println(string(info))
}

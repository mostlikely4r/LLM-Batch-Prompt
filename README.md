# LLM-Batch-Prompt
A utility program to batch prompt multiple models using koboldcpp to quickly compare the output of different models.
![Simple output](https://github.com/mostlikely4r/LLM-Batch-Prompt/blob/main/simple_output.png?raw=true)


See also config.conf.

This program will start-up koboldcpp with a number of models and batch-prompt to get the different responses.
The responses are exported to a .json file to quicky see the difference between the models.

Inspired by Weicon's model rating script.

Used libraries: 
https://github.com/open-source-parsers/jsoncpp
https://github.com/elnormous/HTTPRequest

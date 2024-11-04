from openai import OpenAI
import time
import json
import re

client = OpenAI()
conversation_history = []

prompt = """Context:

You are an expert performance engineer who has high expertise in optimizing code. We will give you a code and we want you to accelerate its execution time by applying code optimizations. You will have a list of code optimizations from which you can select. You should optimize the code for an Intel CPU with 12 cores (Intel Xeon E5-2695v2 CPU).
"""
        
prompt += """Your task:

We will give you the initial program code written in the C language and a list of code optimizations. We will ask you to select the best optimizations that we should apply to the code. Your goal more precisely is to identify what are the loop optimizations that maximize parallelism and data locality in the code. Each time you select a code optimization, we will apply it to the code, if it is valid (legal).

Once you finish selecting optimizations, we will apply loop parallelization, loop tiling and loop unrolling if these are legal.
"""
        
prompt += """Input Code:

Here is the input code that you should optimize:
"""

try:
    while True:
        initial_program_ast = input()
        computations_buffers_json = input()

        computations_buffers_json = json.loads(computations_buffers_json)
        initial_program_ast = initial_program_ast.replace("$", "\n")
            
        for comp_name, comp_buffer in computations_buffers_json.items():
            initial_program_ast = initial_program_ast.replace(comp_name+"(", comp_buffer+"[")
            
        # Pattern to find index patterns ending with ')' and replace ')' with ']'
        initial_program_ast = re.sub(r'\[([^\]]+?)\)', r'[\1]', initial_program_ast)
        
        if len(conversation_history) == 0:
            prompt += initial_program_ast
            prompt += """Transformations:

You have the following list of code optimizations to select from

1- Loop interchange.
2- Loop reversal.
3- Loop skewing.
4- None.

"None" indicates that there are no more interesting optimizations to apply. 

From the above list, can you please select the next most important optimization that will maximize parallelism and/or data locality when applied to the code?

Please respond with only the index of the code optimization: """
        else:
            if initial_program_ast == "":
                if int(completion.choices[0].message.content) == 1:
                    prompt = "Loop interchange is not possible in this case. Which optimization to apply then? "
                if int(completion.choices[0].message.content) == 2:
                    prompt = "Loop reversal is not possible in this case. Which optimization to apply then? "
                if int(completion.choices[0].message.content) == 3:
                    prompt = "Loop skewing is not possible in this case. Which optimization to apply then? "
            else:
                prompt = "Which optimization to apply next? "
                
        with open("./prompts_log.txt", "a") as f:
            f.write(prompt)

        got_response = False
        nb_retries = 0
        
        if len(conversation_history) == 0:
            conversation_history = [
                        {"role": "system", "content": "You are an expert performance engineer who has high expertise in optimizing code."},
                        {
                            "role": "user",
                            "content": prompt
                        }
                    ]
        else:
            conversation_history.append({"role": "user", "content": prompt})
        while not got_response:
            try:
                completion = client.chat.completions.create(
                    model="gpt-4o",
                    messages=conversation_history
                )
                got_response = True
            except Exception:
                if nb_retries > 10:
                    raise Exception("Max retries exceeded. Please try again later.")
                time.sleep(2*(nb_retries+1))
                nb_retries += 1
        
        conversation_history.append({"role": "assistant", "content": completion.choices[0].message.content})
        with open("./prompts_log.txt", "a") as f:
            f.write(completion.choices[0].message.content + "\n")
        
        print(int(completion.choices[0].message.content))

except EOFError:
        exit()
except BrokenPipeError:
        exit()


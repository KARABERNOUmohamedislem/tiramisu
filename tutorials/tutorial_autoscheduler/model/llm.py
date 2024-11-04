from openai import OpenAI
import time
import json
import re

client = OpenAI()

try:
    while True:
        initial_program_ast = input()
        computations_buffers_json = input()
        schedules_json = input()

             
        schedules_json = json.loads(schedules_json)
        computations_buffers_json = json.loads(computations_buffers_json)
        initial_program_ast = initial_program_ast.replace("$", "\n")
            
        for comp_name, comp_buffer in computations_buffers_json.items():
            initial_program_ast = initial_program_ast.replace(comp_name+"(", comp_buffer+"[")
            
        # Pattern to find index patterns ending with ')' and replace ')' with ']'
        initial_program_ast = re.sub(r'\[([^\]]+?)\)', r'[\1]', initial_program_ast)
        
        
        prompt = "Context: Given an initial program code, we want to accelerate its execution time by applying code transformations. We do so by exploring a search space of transformations and keeping the ones with the best runtime speedup. The speedup is calculated by executing the initial and transformed program in an Ubuntu 22.04.3 system running on a dual-socket 12-core Intel Xeon E5-2695v2 CPU equipped with 128 GB of RAM. I will give you more information about the search space, your task and the inputs you need.\nSearch space explanation: The transformation search space is structured as a tree where each node represents a primitive transformation and a branch is an ordered sequence of transformations. For the search we use beam search to explore the tree where given an evaluation function of runtime speedup, we pick K elements for each level. we structure our candidate generation algorithm depending on the level we are at in the tree: At the first level of the tree (where the root is the original, unmodified program), we generate possible loop fusion candidates for the input program AST. After exploring the possible loop fusions and picking the best candidates to explore further, we explore affine transformations where we explore an n-long sequence of interchange, skewing, reversal, and their parameters. The best candidates from the previous levels (fusion and affine transformations) are set to explore the following transformations in this order: parallelization, tiling, and unrolling.\n"
        
        prompt += "Your task: I will give you the initial program code written in C language and a set of tree branchs (candidates) of the ordered sequence of already applied transformations and the new explored transformations, that has not been accepted in our search exploration. Your goal is to assess if some of the candidates will be promising in the later exploration stages, to add them again to our search algorithm.\n"
        
        prompt += """Inputs:
Initial program code: The initial program is written in C language with the following components:
Nested Loops: Each loop is labeled by depth (e.g., L0:, L1:) with specified bounds (e.g., for(i = 0; i < 20; i++)), controlling iteration levels.
Computation names: Indicate the computation name that will be used in describing the transformations.
Assignments and Calculations: Each statement updates matrices or arrays using functions or expressions, incrementally building the overall result.
Indentation and Structure: Indentation reflects loop depth, making the logical flow of operations clear.

Transformations: The candidates that you will choose from will be represented as with two parts:
Already applied transformations: Transformations already accepted in the tree branch.
New explored transformations: The transformations that were rejected when added to the previous set of transformations.
The transformations will be represented in the following way:
FUSION loop_level computation_name1 then computation_name2 : Fusing computation_name1 with computation_name2 in this order at loop_level.
SHIFTING loop_level +shifting_factor computation_name: Applying shifting to the computation_name at loop_level with shifting_factor.
INTERCHANGE loop_level1 loop_level2 {computation_names}: Applying loop interchange between loop_level1 and loop_level2 for computation_names.
REVERSAL loop_level  {computation_names}: Applying loop reversal for computation_names at loop_level.
SKEWING loop_level1 loop_level2 loop_level3 skew_parameter_1 skew_parameter_2 skew_parameter_3 skew_parameter_4 skew_parameter_5 skew_parameter_6 skew_parameter_7 skew_parameter_8 skew_parameter_9  {computation_names}: Applying skewing for computation_names at  loop_level1, loop_level2, and loop_level3 with the skewing parameters skew_parameter1, …, skew_parameter9.
TILING loop_level1 tiling_parameter1 {computation_names}: Applying loop tiling to computation_names at  loop_level1 with tiling_parameter1.
TILING loop_level1 tiling_parameter1 loop_level2 tiling_parameter2 {computation_names}: Applying loop tiling (2D) to computation_names at  loop_level1 with tiling_parameter1 and loop_level2 with tiling_parameter2.
TILING loop_level1 tiling_parameter1 loop_level2 tiling_parameter2 loop_level3 tiling_parameter3 {computation_names}: Applying loop tiling (3D) to computation_names at  loop_level1 with tiling_parameter1 and loop_level2 with tiling_parameter2 and loop_level3 with tiling_parameter3.
UNROLLING loop_level unrolling_factor {computation_names}: Applying loop unrolling to computation_names at loop_level with unrolling_factor.
PARALLELIZE loop_level {computation_names}: Applying loop parallelization for computation_names at loop_level.
VECTORIZATION loop_level vectorization_parameter {computation_names}: Applying loop vectorization to computation_names at loop_level with vectorization_parameter.
no transformations: No transformation was applied.

Now that I explained everything, here are your inputs:
Initial program code:
\n"""
        prompt += initial_program_ast
        if len(schedules_json.items()) > 1:
            if len(schedules_json.items()) < 5 :
                prompt += "\nFrom the following list, give me the indices of the candidates you think can be promising for further exploration:\n"
            else:
                prompt += "\nFrom the following list, give me the indices of at most 5 candidates you think can be promising for further exploration:\n"
        else:
            prompt += "\nIf you think this set of transformations can be promising for further exploration, answer with 0, else with -1:\n"
        
        for i, tranformations in schedules_json.items():
            if len(schedules_json.items()) > 1:
                prompt += str(i) + ". "
            prompt += "Already applied transformations:\n"
            if (tranformations["previous_optims"] == ""):
                prompt += "no transformations\n"
            else:
                prompt += tranformations["previous_optims"].replace(";", "\n")
                
            prompt += "New explored transformations:\n"
            if (tranformations["new_optims"] == ""):
                prompt += "no transformations\n"
            else:
                prompt += tranformations["new_optims"].replace(";", "\n")
        
        if len(schedules_json.items()) > 1:
            prompt += "Please respond with only the indices seperated by a semi-colon: "
        else:
            prompt += "Please respond with only 0 or -1: "
        with open("./prompts_log.txt", "a") as f:
            f.write(prompt)

        got_response = False
        nb_retries = 0
        
        while not got_response:
            try:
                completion = client.chat.completions.create(
                    model="gpt-4o",
                    messages=[
                        {"role": "system", "content": "You are an expert performance engineer who has high expertise in optimizing code. \nYour goal is to optimize the following code and make it run faster."},
                        {
                            "role": "user",
                            "content": prompt
                        }
                    ]
                )
                got_response = True
            except Exception:
                if nb_retries > 10:
                    raise Exception("Max retries exceeded. Please try again later.")
                time.sleep(2*(nb_retries+1))
                nb_retries += 1
        
        with open("./prompts_log.txt", "a") as f:
            f.write(completion.choices[0].message.content + "\n")
        
        tab = completion.choices[0].message.content.split(";")

        for i in tab:
            if i != "":
                print(int(i))
        if -1 not in tab:
            print(-1)
except EOFError:
        exit()
except BrokenPipeError:
        exit()


from openai import OpenAI
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
        
        
        prompt = "Context: Given an initial program code, a search algorithm explores a large space of code transformations. The transformation search space is structured as a tree where each node represents a primitive transformation and a branch is an ordered sequence of transformations. For the search we use beam search to explore the tree where given an evaluation function that quantifies the quality of each branch in the tree, we pick K elements for each level. we structure our candidate generation algorithm depending on the level we are at in the tree: At the first level of the tree (where the root is the original, unmodified program), we generate possible loop fusion candidates for the input program AST. After exploring the possible loop fusions and picking the best candidates to explore further, we explore affine transformations where we explore an n-long sequence of interchange, skewing, reversal, and their parameters. The best candidates from the previous levels (fusion and affine transformations) are set to explore the following transformations in this order: parallelization, tiling, and unrolling."
        prompt += "\nInstruction: I will give you a pseudo code that represents the initial program and a set of tree branchs of the ordered sequence of already applied transformations and the new explored transformations, that has not been accepted in our search exploration. Your goal is to assess if some of the candidates will be promising in the later exploration stages.\n"
        prompt += "The pseudo code format consists of:\nNested Loops: Each loop is labeled by depth (e.g., 0-, 1-, 2-) with specified bounds (e.g., 0 <= k < 30), controlling iteration levels.\nComputation names: (e.g., nrm_init, R_diag, Q_out) indicate the computation name that will be used in describing the transformations.\nAssignments and Calculations: Each statement updates matrices or arrays (b_nrm, b_R, b_Q, etc.) using functions (like nrm or sqrt) or expressions, incrementally building the overall result.\nIndentation and Structure: Indentation reflects loop depth, making the logical flow of operations clear.\n"
        prompt += "The transformations format is: Transformation_type Loop_levels_affected Parameters {Computations_affected}\n"
        prompt += "Pseudo code:\n"+ initial_program_ast
        if len(schedules_json.items()) > 1:
            prompt += "\nFrom the following list, give me the indices of the transformations you think can be promising for further exploration:\n"
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
        with open("./demofile.txt", "a") as f:
            f.write(prompt)
        
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
        
        with open("./demofile.txt", "a") as f:
            f.write(completion.choices[0].message.content)
        
        tab = completion.choices[0].message.content.split(";")

        for i in tab:
            print(int(i))
        if -1 not in tab:
            print(-1)
except EOFError:
        exit()
except BrokenPipeError:
        exit()


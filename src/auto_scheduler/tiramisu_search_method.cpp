#include <sys/wait.h>
#include <tiramisu/auto_scheduler/search_method.h>
#include <random>

#include <random>
#include <functional>
#include <exception>

#include <stdexcept>

namespace tiramisu::auto_scheduler
{
    // list of hashes of matrices we explored before to avoid repeating schedules. Used in search_save_matrix
std::vector<std::size_t> hashes;

void beam_search::setup_llm_pipeline()
{
    const std::string TIRAMISU_ROOT = std::getenv("TIRAMISU_ROOT");
    // Create the pipe
    pid_t pid = 0;
    int inpipe[2];
    int outpipe[2];
    
    pipe(inpipe);
    pipe(outpipe);
    std::string const &cmd_path = std::getenv("PYTHON_PATH");
    std::vector<std::string> const &cmd_args = {TIRAMISU_ROOT  + "/tutorials/tutorial_autoscheduler/model/llm.py"};

    pid = fork();
    if (pid == 0)
    {
        dup2(outpipe[0], STDIN_FILENO);
        dup2(inpipe[1], STDOUT_FILENO);
        
        close(outpipe[1]);
        close(inpipe[0]);
        
        // Here we are in a new process.
        // Launch the program that evaluates schedules with the command cmd_path,
        // and arguments cmd_args.
        char* argv[cmd_args.size() + 2];
        
        argv[0] = (char*)malloc(sizeof(char) * (cmd_path.size() + 1));
        strcpy(argv[0], cmd_path.c_str());
        argv[cmd_args.size() + 1] = NULL;
        
        for (int i = 0; i < cmd_args.size(); ++i) {
            argv[i + 1] = (char*)malloc(sizeof(char) * (cmd_args[i].size() + 1));
            strcpy(argv[i + 1], cmd_args[i].c_str());
        }
        
        execv(cmd_path.c_str(), argv);
        exit(1);
    }
    
    close(outpipe[0]);
    close(inpipe[1]);
    
    llm_write = fdopen(outpipe[1], "w");
    llm_read = fdopen(inpipe[0], "r");
}

std::vector<int> beam_search::prompt_llm(std::string initial_program_ast, std::string computations_buffers_json, std::vector<syntax_tree*> candidates){
    
    if (candidates.size() == 0) return std::vector<int>();
    std::string schedules_json = "{";
    
    for (int i = 0; i < candidates.size(); i++)
    {  
        schedules_json += "\"" + std::to_string(i) + "\" : { \"previous_optims\" : \"";
        
        for (optimization_info optim: candidates[i]->previous_optims)
        {
            if (optim.type != optimization_type::MATRIX || optim.unimodular_transformation_type != 0)
            {
                schedules_json += get_optim_str(optim) + ";";
            }
            
        }
        schedules_json += "\", \"new_optims\" : \"";
        for (optimization_info optim: candidates[i]->new_optims)
            if (optim.type != optimization_type::MATRIX || optim.unimodular_transformation_type != 0)
            {
                schedules_json += get_optim_str(optim) + ";";
            }
        
        schedules_json += "\"},";
      
    }
    schedules_json.pop_back();
    schedules_json += "}\n";
    
    // Send the necessary information for the prompt
    fputs(initial_program_ast.c_str(), llm_write);
    fputs(computations_buffers_json.c_str(), llm_write);
    fputs(schedules_json.c_str(), llm_write);
    fflush(llm_write);
    
    // // Read the response from llm_read
    int response;
    std::vector<int> indices;
    while (response != -1)
    {
        fscanf(llm_read, "%d", &response);
        if (response == -1) break;
        else indices.push_back(response);
    }

    // needs parsing
    return indices;
}
    
void beam_search::explore_schedules(syntax_tree &ast, std::vector<std::string> *schedules_annotations, candidate_trace *parent_trace, float schedule_timeout ){
    
    std::queue<syntax_tree*> exploration_queue;
    exploration_queue.push(&ast);
    std::unordered_map<syntax_tree*, candidate_trace*> trace_map;
    
    std::string initial_program_ast = ast.get_ast_str();
    initial_program_ast = initial_program_ast + "\n"; // Adding line break to use it as an input to the python file
    std::string computations_buffers_json = ast.get_computations_buffers_mapping_json();
    std::vector<int> candidates_to_keep_indices;
    std::vector<syntax_tree*> excluded_candidates;
    bool keep_candidate;
    setup_llm_pipeline();
    
    while(!exploration_queue.empty()){
        
        trace_map[&ast] = parent_trace;
        
        std::vector<syntax_tree*> level_schedules;
        while(!exploration_queue.empty()){
            
            syntax_tree *ast_to_explore = exploration_queue.front();

            // Clear new optims to explore new optimizations
            ast_to_explore->clear_new_optimizations();
            exploration_queue.pop();
            std::vector<syntax_tree*> intermediate_schedules ;
            
            switch(ast_to_explore->ast_search_phase) {

                case search_phase::FUSION:
                    
                    intermediate_schedules = search_save(*ast_to_explore, schedules_annotations, trace_map[ast_to_explore], schedule_timeout);
                    break;

                case search_phase::UNIMODULAR:
                    
                    intermediate_schedules = search_save_matrix(*ast_to_explore, schedules_annotations, trace_map[ast_to_explore], schedule_timeout);
                    break;  

                case search_phase::NON_UNIMODULAR:
                    
                    intermediate_schedules = search_save(*ast_to_explore, schedules_annotations, trace_map[ast_to_explore], schedule_timeout);
                    break;
                
                default:
                    return;
            }
            for(auto sched: intermediate_schedules){
                    trace_map[sched] = trace_map[ast_to_explore]->child_mappings[sched];
                    
            }
            level_schedules.insert(level_schedules.end(), intermediate_schedules.begin(), intermediate_schedules.end());
        }
        //Sort children from smallest evaluation to largest
        std::sort(level_schedules.begin(), level_schedules.end(), [](syntax_tree *a, syntax_tree *b) {
            return a->evaluation < b->evaluation;
        });
        
        // Indices should start from 0
        if (excluded_candidates.size() > 0) excluded_candidates.clear();
        
        for (int i = beam_size; i < level_schedules.size(); i++)
        {
            excluded_candidates.push_back(level_schedules[i]);
        }
        candidates_to_keep_indices = prompt_llm(initial_program_ast, computations_buffers_json, excluded_candidates);
        
        // Keep beam_size first elements, plus the elements from the LLM
        for (int i = 0; i < level_schedules.size(); i++)
        {   
            if (i < beam_size){
                exploration_queue.push(level_schedules[i]);
            }
            else {
                keep_candidate = (std::find(candidates_to_keep_indices.begin(), candidates_to_keep_indices.end(), i-beam_size) != candidates_to_keep_indices.end());
                
                if (keep_candidate){
                    exploration_queue.push(level_schedules[i]);
                }
            }
        }
        std::cout << "\nBeam size : " << beam_size << "\nKept candidate (beam size + candidates from the LLM) : " << exploration_queue.size() << std::endl;
    }
}
/*
returns identity matrix
*/
std::vector <  std::vector<int> > get_identity(int depth){
    std::vector <  std::vector<int> >  matrix(depth);
        for(int l = 0; l<matrix.size(); l++){
            matrix.at(l)= std::vector<int>(depth);
            for(int c = 0; c<matrix.size(); c++){
                            if (l!=c ){
                                matrix.at(l).at(c) = 0;
                            }else{
                                matrix.at(l).at(c) = 1;
                            }
            }
        }
        return matrix;
}

std::vector<syntax_tree*> beam_search::search_save_matrix(syntax_tree& ast, std::vector<std::string> *schedules_annotations, candidate_trace *parent_trace, float schedule_timeout)
{
        
    std::default_random_engine rand_generator;
    std::vector<syntax_tree*> children;
    // list of ASTs to be explored for next level 
    std::vector<syntax_tree*> to_be_explored;
    std::hash<std::string> hasher;

    // at this stage we only explore matrices
    std::vector<optimization_type> optims;
    optims.push_back(optimization_type::MATRIX);
    ast.initialize_search_space_optimizations(optims);

    // if this is the root of the exploration tree 
    // we want to create the original schedules which will include identity matrices only
    // to know the size of the matrix for each computation, we go through all the tree looking for nodes that have computations. 
    // the depth of this node is the size of the matrix for all the computations it contains 
    if (ast.search_depth==1){
        
        std::vector<ast_node*> nodes;
        // go through each root of the tree to recover all computations
        for(auto root: ast.roots){
            std::vector<ast_node*> nodes;
            root->get_all_nodes(nodes);
            for(auto node : nodes){
                if(node->computations.size()>0){
                    optimization_info optim_info;
                    optim_info.type = optimization_type::MATRIX;
                    node->get_node_computations(optim_info.comps);
                    optim_info.node = node;
                    // for the original schedule, the transformation matrix is the identity
                    optim_info.matrix = get_identity(node->depth+1);
                    ast.new_optims.push_back(optim_info);
                }   
            }
        }  
    }
    // add the hash of this tree to avoid exploring the same schedules twice
    hashes.push_back(hasher(ast.get_schedule_str()));
    while ((!ast.is_search_space_empty()))
    {
        // schedule generation based on generator_state attribute in the AST.
        auto new_children = scheds_gen->generate_matrices(ast);
        for(auto& child:new_children)
            child->move_to_next_head();
        
        children.insert(children.end(), new_children.begin(), new_children.end()); // concatenate

        if  (ast.search_state.is_current_optimization_fully_explored() && !children.empty()) {
            // move to next optimization
            // explores next optimization/alternative
            ast.move_to_next_head();
            
            break;
        }
        else
            ast.move_to_next_head();
    }

    // if no candidates were generated, return an empty list
    if (children.size() == 0) return children;

    // hash the parent 
    std::size_t parent_hash=hasher(ast.get_schedule_str());

    auto iterator = children.begin();
    std::vector<std::vector<std::vector<int>>> repeated;
    
    syntax_tree *child;
    // evaluate the legal children and sort them from smallest to highest evaluation
    while (iterator != children.end())
    {
        child = *iterator;
        child->transform_ast();
        if(child->ast_is_prunable()){
            if (std::atoi(read_env_var("AS_VERBOSE"))==1){
                    // print deleted Ast
                    child->print_previous_optims();
                    std::cout << "-----------" << std::endl;
                    child->print_new_optims();
                    child->print_ast();
                    child->print_isl_states();
                    std::cout << "\n<surpassed MAX_MAT_DEPTH>\n";
                }
                delete child;
                iterator = children.erase(iterator);
        }else{
            if (!child->ast_is_legal()) {
                if (std::atoi(read_env_var("AS_VERBOSE"))==1){
                    // print deleted Ast
                    child->print_previous_optims();
                    std::cout << "-----------" << std::endl;
                    child->print_new_optims();
                    
                    child->print_ast();
                    child->print_isl_states();
                    std::cout << "\n<illegal>" << std::endl << std::endl;;
                }
                delete child;
                iterator = children.erase(iterator);
            }else{
                // hash the legal schedule
                std::size_t hash=hasher(child->get_schedule_str());
                
                bool repeated = false;
                // check if we explored this matrix before  
                for(std::size_t hashe:hashes){
                    if(hashe==hash){
                        //if that's the case remove the child from the exploration tree
                        delete child;
                        iterator = children.erase(iterator);
                        repeated = true;
                        break;
                    }
                }
                if(repeated) continue;

                // if the matrix is legal and not repeated we add its hash to the list of seen hashes and we start the evaluation 
                hashes.push_back(hash);
                
                // print and evaluate Ast
                if (std::atoi(read_env_var("AS_VERBOSE"))==1){
                    child->print_previous_optims();
                    std::cout << "-----------" << std::endl;
                    child->print_new_optims();
                    child->print_ast();
                    child->print_isl_states();
                    std::cout << "\n<legal>\n";
                    child->print_computations_accesses();
                }

                std::vector<float> measurements;
                // check the environment variable EXPLORE_BY_EXECUTION to decide the evaluation method
                if(std::atoi(read_env_var("EXPLORE_BY_EXECUTION"))==1){
                    measurements = exec_eval->get_measurements(*child, false, schedule_timeout);
                }else{
                    std::string no_sched_json = schedules_annotations->at(0);
                    measurements.push_back(eval_func->evaluate(*child, no_sched_json));
                }
                child->evaluation = min_eval(measurements);
                
                if(hash != parent_hash) child->nb_explored_matrices = child->nb_explored_matrices +1; 
                
                // add the child to the exploration trace
                parent_trace->add_child_path(child, schedules_annotations->size());
                
                std::string schedule_annot = evaluate_by_learning_model::get_schedule_json(*child);
                
                //remove the last two characters }\n
                schedule_annot.pop_back();
                schedule_annot.pop_back();
                
                if (std::isfinite(child->evaluation)) // the evaluation is not finite mean that the schedule didn't run
                    schedule_annot += ", \n\"execution_times\" : " + measurements_to_str(measurements) + "\n}\n";
                else
                    schedule_annot += ", \n\"execution_times\" : null\n}\n";

                schedules_annotations->push_back(schedule_annot);
                
                if (std::atoi(read_env_var("AS_VERBOSE"))==1){
                    std::cout << "Schedule number "<< schedules_annotations->size() << std::endl;
                    std::cout << "Evaluation : " << child->evaluation << std::endl;
                    std::cout << "Number of measurements : " << measurements.size() << std::endl;
                    std::cout << "===================================" << std::endl << std::endl;
                }

                if (std::isinf(child->evaluation))
                    std::cerr<< "Evaluation of schedule "<< schedules_annotations->size() <<" failed "<< std::endl;

                if (child->evaluation < best_evaluation)
                {
                    best_evaluation = child->evaluation;
                    best_ast = child;
                }
                
                to_be_explored.push_back(child);
                
                ++iterator;  
                
            }
        }
    }
    // add the possibility to explore no transformation at this level by adding a copy of the parent to the list of candidates
    syntax_tree *ast_copy = ast.copy_ast();
    to_be_explored.push_back(ast_copy);
    parent_trace->add_child_path(ast_copy, parent_trace->get_candidate_id());
    
    // we explore MAX_MAT_DEPTH matrices per computations
    int nb_comps= ast.get_innermost_nodes().size();
    for (syntax_tree *child : to_be_explored)
    {
        // increment the search depth for the recursive call
        child->search_depth = child->search_depth + 1;
        // if we are NOT under the maximum depth of matrices to explore then call search_move on to the next exploration phase
        if (!(child->search_depth< MAX_MAT_DEPTH * nb_comps && child->search_depth-1 <= child->nb_explored_matrices)){
            child->initialize_search_space_optimizations(DEFAULT_OPTIMIZATIONS_ORDER);
            // if we surpassed the MAX_MAT_DEPTH amount of matrices to explore OR we detected the parent of this level through
            // the child->search_depth<=child->nb_explored_matrices condition which means that the search level is greater than the number of applied matrices
            // reinitialize current index to zero for the next level of exploration
            child->search_state.current_index = 0;
            child->search_state.optimization_index = 0;
            child->ast_search_phase = search_phase::NON_UNIMODULAR;
        }
    }
    return to_be_explored;
}
std::vector<syntax_tree*> beam_search::search_save(syntax_tree& ast, std::vector<std::string> *schedules_annotations, candidate_trace *parent_trace, float schedule_timeout)
{
    std::vector<syntax_tree*> children;
    std::vector<optimization_type> transformations_to_explore;
    if(ast.ast_search_phase == search_phase::FUSION){
        transformations_to_explore.push_back(optimization_type::FUSION);
    }else{
        transformations_to_explore = DEFAULT_OPTIMIZATIONS_ORDER;
    }

    if(generator_state::initialized == false)
    {
        ast.initialize_search_space_optimizations(transformations_to_explore);
        // the optimizations are specified along with the parameters in the generator_state attribute inside the AST.
        assert(generator_state::initialized == true);
    }

    while ((!ast.is_search_space_empty()))
    {
        // schedule generation based on generator_state attribute in the AST.
        auto new_children = scheds_gen->generate_schedules(ast);
        
        for(auto& child:new_children)
            child->move_to_next_optimization_target();

        children.insert(children.end(), new_children.begin(), new_children.end()); // concatenate

        if  (ast.search_state.is_current_optimization_fully_explored() && !children.empty()) {
            // move to next optimization
            // explores next optimization/alternative
            ast.move_to_next_optimization_target();
            break;
        }
        else
            ast.move_to_next_optimization_target();
    }
    
    
    // Stop if no more optimizations can be applied
    // Unless we are exploring fusion. SInce Fusion is seperated from the other transformations, even if no fusion candidates are available, we explore the root.
    if (children.size() == 0 && ast.ast_search_phase != search_phase::FUSION)
        return children;
    

    // Evaluate children and sort them from smallest to highest evaluation
    // evaluate while removing illegal versions
    auto iterator = children.begin();
    while (iterator != children.end())
    {
        (*iterator)->transform_ast();
        if ((*iterator)->ast_is_legal() == false) {
            // print deleted Ast

            if (std::atoi(read_env_var("AS_VERBOSE"))==1){
                (*iterator)->print_previous_optims();
                std::cout << "-----------" << std::endl;
                (*iterator)->print_new_optims();
                (*iterator)->print_ast();
                (*iterator)->print_isl_states();
                std::cout << "\n<illegal>" << std::endl << std::endl;
            }
            delete (*iterator);
            iterator = children.erase(iterator);
            
        }
        else {

            // evaluate and print Ast
            if (std::atoi(read_env_var("AS_VERBOSE"))==1){
                (*iterator)->print_previous_optims();
                std::cout << "-----------" << std::endl;
                (*iterator)->print_new_optims();
                (*iterator)->print_ast();
                std::cout << "\n<legal>\n";
            }
            
            std::vector<float> measurements;
                                    
            if(std::atoi(read_env_var("EXPLORE_BY_EXECUTION"))==1){
                measurements = exec_eval->get_measurements(**iterator, false, schedule_timeout);
            }else{
                std::string no_sched_json = schedules_annotations->at(0);
                measurements.push_back(eval_func->evaluate(*(*iterator), no_sched_json));
            }
                    
            
            (*iterator)->evaluation = min_eval(measurements);
            parent_trace->add_child_path((*iterator), schedules_annotations->size());

            std::string schedule_annot = evaluate_by_learning_model::get_schedule_json(*(*iterator));

            //remove the last two characters }\n
            schedule_annot.pop_back();
            schedule_annot.pop_back();

            if (std::isfinite((*iterator)->evaluation)) // the evaluation is not finite mean that the schedule didn't run
                schedule_annot += ", \n\"execution_times\" : " + measurements_to_str(measurements) + "\n}\n";
            else
                schedule_annot += ", \n\"execution_times\" : null\n}\n";

            schedules_annotations->push_back(schedule_annot);

            std::cout << "Schedule number "<< schedules_annotations->size() << std::endl;
            std::cout << "Evaluation : " << (*iterator)->evaluation << std::endl;
            std::cout << "Number of measurements : " << measurements.size() << std::endl;
            std::cout << "===================================" << std::endl << std::endl;

            if (std::isinf((*iterator)->evaluation))
                std::cerr<< "Evaluation of schedule "<< schedules_annotations->size() <<" failed "<< std::endl;

            if ((*iterator)->evaluation < best_evaluation)
            {
                best_evaluation = (*iterator)->evaluation;
                best_ast = (*iterator);
            }
            
            ++iterator;

        }

            nb_explored_schedules++;
    }

    // Add the current AST to the list of children
    syntax_tree *ast_copy = ast.copy_ast();
    children.push_back(ast_copy);

    parent_trace->add_child_path(ast_copy, parent_trace->get_candidate_id()); // keeps the same id since it's just copy

    // Sort children from smallest evaluation to largest
    for (syntax_tree *child : children)
    {
        if(child->ast_search_phase == search_phase::FUSION){
            // reinitialize current index to zero for the next level of exploration
            child->search_state.current_index = 0;
            child->ast_search_phase = search_phase::UNIMODULAR;
        }
        child->search_depth = ast.search_depth + 1;
    }
    return children;
}

void beam_search::search(syntax_tree& ast)
{
    std::vector<syntax_tree*> children;
    // Look for an optimization that can be applied
    if(generator_state::initialized == false)
    {
        ast.initialize_search_space_optimizations(DEFAULT_OPTIMIZATIONS_ORDER);
        // the optimizations are specified along with the parameters in the generator_state attribute inside the AST.
        assert(generator_state::initialized == true);
    }
    
    while ((!ast.is_search_space_empty()))
    {
        // schedule generation based on generator_state attribute in the AST.
        auto new_children = scheds_gen->generate_schedules(ast);

        for(auto& child:new_children)
        {
            child->move_to_next_optimization_target();
        }
        
        children.insert(children.end(), new_children.begin(), new_children.end()); // concatenate
        if  (ast.search_state.is_current_optimization_fully_explored() && !children.empty()) {
            // move to next optimization
            // explores next optimization/alternative
            ast.move_to_next_optimization_target();
            break;
        }
        else
            ast.move_to_next_optimization_target();
    }
       
    // Stop if no more optimizations can be applied
    if (children.size() == 0)
        return ;
       
    // Evaluate children and sort them from smallest to highest evaluation
    // evaluate while removing illegal versions
    auto iterator = children.begin();
    while (iterator != children.end())
    {

        (*iterator)->transform_ast();

        if ((*iterator)->ast_is_legal() == false) {

            // print deleted Ast 
            (*iterator)->print_previous_optims();
            std::cout << "\n-----------" << std::endl;
            (*iterator)->print_new_optims();
            (*iterator)->print_ast();
            (*iterator)->print_isl_states();
            std::cout << "\n<illegal>\n";
            delete (*iterator);
            iterator = children.erase(iterator);
        }
        else {

            // evaluate and print Ast
            (*iterator)->print_previous_optims();
            std::cout << "-----------" << std::endl;
            (*iterator)->print_new_optims();
            (*iterator)->print_ast();

            std::cout << "\n<legal>\n";

            (*iterator)->evaluation = eval_func->evaluate(*(*iterator));
            std::cout << "Evaluation : " << (*iterator)->evaluation << std::endl << std::endl;

            std::cout << "\n============================================================================================================" << std::endl;

            if ((*iterator)->evaluation < best_evaluation)
            {
                best_evaluation = (*iterator)->evaluation;
                best_ast = (*iterator);
            }

            ++iterator;

        }
        
        nb_explored_schedules++;
    }

        
    // Add the current AST to the list of children
    syntax_tree *ast_copy = ast.copy_ast();
    children.push_back(ast_copy);

    // Sort children from smallest evaluation to largest


    std::sort(children.begin(), children.end(), [](syntax_tree *a, syntax_tree *b) {
        return a->evaluation < b->evaluation;
    });

    // keep the top 'beam_size' children and delete the rest
    for (int i = beam_size; i < children.size(); ++i)
        delete children[i];
    
        
    children.resize(std::min(beam_size, (int)children.size()));

    // Search recursively on the best children
    for (syntax_tree *child : children)
    {
        child->search_depth = ast.search_depth + 1;        
        search(*child);
    }
}
}

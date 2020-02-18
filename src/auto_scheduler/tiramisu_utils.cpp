#include <tiramisu/auto_scheduler/utils.h>
#include <tiramisu/auto_scheduler/ast.h>

namespace tiramisu::auto_scheduler
{

void dnn_access_matrix::create_accesses(tiramisu::expr const& e, int nb_iterators,
                                        std::vector<dnn_access_matrix>& accesses, 
                                        tiramisu::computation *comp)
{   
    // Not an operation, stop the search
    if (e.get_expr_type() != tiramisu::e_op)
        return ;
        
    // We have an access, so we add its access matrix
    if (e.get_op_type() == tiramisu::o_access || 
        e.get_op_type() == tiramisu::o_lin_index ||
        e.get_op_type() == tiramisu::o_address_of || 
        e.get_op_type() == tiramisu::o_dummy ||
        e.get_op_type() == tiramisu::o_buffer)
    {
        accesses.push_back(dnn_access_matrix(nb_iterators, e, comp));
        return ;
    }
    
    // We have an operation, we explore its operands
    for (int i = 0; i < e.get_n_arg(); ++i)
        create_accesses(e.get_operand(i), nb_iterators, accesses, comp);
}

dnn_access_matrix::dnn_access_matrix(int nb_iterators, int nb_dims)
    : nb_iterators(nb_iterators), nb_dims(nb_dims), matrix(nb_dims), buffer_id(0)
{
    for (int i = 0; i < nb_dims; ++i)
        matrix[i] = std::vector<int>(nb_iterators + 1, 0);
}
    
dnn_access_matrix::dnn_access_matrix(int nb_iterators, tiramisu::expr const& e, tiramisu::computation *comp)
    : dnn_access_matrix(nb_iterators, e.get_access().size())
{
    this->comp = comp;
    std::vector<tiramisu::expr> const& acc_vector = e.get_access();

    for (int i = 0; i < acc_vector.size(); ++i)
        fill_matrix_row(i, acc_vector[i]);
    
    buffer_name = e.get_name();
}

void dnn_access_matrix::fill_matrix_row(int i, tiramisu::expr const& e, bool minus)
{
    if (e.get_expr_type() == tiramisu::e_op)
    {
        // We got : access1 +- access2
        if (e.get_op_type() == o_add || e.get_op_type() == o_sub)
        {
            minus = false;
            if (e.get_op_type() == o_sub)
                minus = true;
                
            fill_matrix_row(i, e.get_operand(0), minus);
            fill_matrix_row(i, e.get_operand(1), minus);
        }
        
        // We got : coefficient * iterator
        else if (e.get_op_type() == o_mul)
        {
            int coeff = e.get_operand(0).get_int32_value();
            int it_pos = comp->get_loop_level_number_from_dimension_name(e.get_operand(1).get_name());
            
            if (minus)
                matrix[i][it_pos] = -coeff;
            else
                matrix[i][it_pos] = coeff;
        }
    }
    
    // Access coefficient == 1
    else if (e.get_expr_type() == tiramisu::e_var)
    {
        int it_pos = comp->get_loop_level_number_from_dimension_name(e.get_name());
        matrix[i][it_pos] = 1;
        
        if (minus)
            matrix[i][it_pos] = -1;
    }
    
    // Constant increment
    else if (e.get_expr_type() == tiramisu::e_val)
    {
        if (minus)
            matrix[i][nb_iterators] = -e.get_int32_value();
        else
            matrix[i][nb_iterators] = e.get_int32_value();
    }
}

void dnn_access_matrix::set_buffer_id(tiramisu::function *fct)
{
    buffer_id = 0;
    for (auto const& map_el : fct->get_buffers())
    {
        if (map_el.first == buffer_name)
            break;
            
        buffer_id++;
    }
}

}
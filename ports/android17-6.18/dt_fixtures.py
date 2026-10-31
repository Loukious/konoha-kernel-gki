import re
from pathlib import Path

def stock_tables(path):
    source=re.sub(r'/\*.*?\*/','',path.read_text(),flags=re.S)
    code=[];profiles=0;rows_count=0
    for label in ['mca_strategy_quick_charge','mca_quick_charge_batt_para_gbl','mca_buckchg_jeita','mca_buckchg_jeita_gbl_para']:
        block=re.search(r'&'+label+r'\s*\{(.*?)\};',source,re.S).group(1)
        props={name:re.findall(r'"([^"]*)"',value) for name,value in re.findall(r'(\w+)\s*=\s*(.*?);',block,re.S)}
        if 'quick' in label:
            for name in sorted({props['batt_para'][i] for i in range(2,len(props['batt_para']),3)}):
                vals=props[name];assert len(vals)%11==0
                rows=[vals[i:i+11] for i in range(0,len(vals),11)];entries=[]
                for i,row in enumerate(rows):
                    numbers=list(map(int,row[:7]));infos=[]
                    for f in range(2):
                        name=row[7+f];curve=props[name] if name!='null' else [];assert len(curve)%3==0
                        var=f'stock_qc_{profiles}_{i}_{f}'
                        if curve:
                            code.append('struct mca_quick_charge_volt_para '+var+'[] = {'+','.join('{'+','.join(curve[j:j+3])+'}' for j in range(0,len(curve),3))+'};')
                            infos.append('{.volt_para_size='+str(len(curve)//3)+',.volt_para='+var+'}')
                        else:infos.append('{0}')
                    entries.append('{.temp_para={'+','.join(map(str,numbers))+'},.volt_info='+infos[0]+',.volt_ffc_info='+infos[1]+'}')
                var=f'stock_qc_{profiles}'
                code.append('struct mca_quick_charge_temp_para_info '+var+'[] = {'+','.join(entries)+'};')
                code.append('struct mca_quick_charge_batt_para_info '+var+'_para = {.temp_para_size=ARRAY_SIZE('+var+'),.temp_info='+var+'};')
                code.append('assert(!mca_qc_validate_batt_para(&'+var+'_para));')
                profiles+=1;rows_count+=len(rows)
        else:
            for name in ['jeita_para','jeita_para_ffc']:
                vals=props[name];assert len(vals)%8==0
                rows=[vals[i:i+8] for i in range(0,len(vals),8)];entries=[]
                for i,row in enumerate(rows):
                    values=dict(zip(['temp_low','temp_high','low_temp_hys','high_temp_hys','max_current','vterm','iterm'],map(int,row[:7])))
                    curve=props[row[7]] if row[7]!='null' else [];assert len(curve)%2==0
                    volt='{0}'
                    if curve:
                        var=f'stock_jeita_{profiles}_{i}';code.append('struct mca_buckchg_jeita_volt_data '+var+'[] = {'+','.join('{'+','.join(curve[j:j+2])+'}' for j in range(0,len(curve),2))+'};');volt='{.size='+str(len(curve)//2)+',.volt_data='+var+'}'
                    entries.append('{'+','.join('.'+key+'='+str(value) for key,value in values.items())+',.volt_para='+volt+'}')
                var=f'stock_jeita_{profiles}';code.append('struct mca_buckchg_jeita_data '+var+'[] = {'+','.join(entries)+'};');code.append('assert(!mca_jeita_validate_table('+var+',ARRAY_SIZE('+var+')));')
                profiles+=1;rows_count+=len(rows)
    return 'static void test_stock_tables(void) {\n'+'\n'.join(code)+'\n}\n', {'profiles':profiles,'temperature_rows':rows_count}

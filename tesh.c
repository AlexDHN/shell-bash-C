#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
// -ldl pour compiler
//#include <readline/readline.h> //sudo apt-get install libreadline-dev 
//#include <readline/history.h>

#define MAX 1024
static char pwd[MAX]; //previous working directory
static char cwd[MAX]; //current working directory
static int option_e = 0; // si tesh -e alors option = 1 sinon 0
static int option_r = 0; // si tesh -r alors option = 1, 0 sinon
static char* (*readline) (const char*);
static char* (*add_history) (const char*);
static int background[MAX] = {'\0'};

void print(char** text){ // fonction de debug, utile pour afficher les instrucitons
    int i = 0;
    while(text[i]!=NULL){
        printf("%s\n",text[i]);
        i++;
    }
    return;
}

void print_d(int* text){ // fonction de debug, utile pour afficher les sorties de background
    int i = 0;
    while(text[i]!='\0'){
        printf("%d\n",text[i]);
        i++;
    }
    return;
}

int cd(char* commande){ // Build-in cd
    int ch;
    if (!strcmp(commande,"-")){
        ch = chdir(pwd);
    }
    else{
        if (!strcmp(commande,"") || !strcmp(commande,"~")){
            ch = chdir("/home");
        }
        else{
            ch = chdir(commande);
        }
    }
    
    if (ch <0){
        return 0;
    }
    else{
        strcpy(pwd,cwd);
        char aux[MAX];
        getcwd(aux,sizeof(aux));   
        strcpy(cwd,aux);
    }
    return 1;
}

int fg(pid_t bg){ // Build_in fg
    int status;
    waitpid(bg,&status,0);
    return(WEXITSTATUS(status));
}

void add_bg(pid_t bg){ // On liste les processus lancé en arrière plan pour la commande fg
    int j = 0;
    while(background[j] != '\0'){
        if(background[j] == -2){
            background[j] = bg;
            return;
        }
        j++;
    }
    background[j] = bg;
    return;
}

int execution(char** instructions, int old_fd, int display){ // Fonction d'éxécution des commandes ; display = 0 on affiche pas et renvoie le descripteur ; 1 on affiche sur stdout ; > 2 on enregistre dans un fichier (display sera le descripteur concerné)
    pid_t fk;
    int fd[2];
    int code; // code d'erreur ou non
    int status;
    if(!strcmp(instructions[0],"false")){ //false doit être traité à part
            
            if (option_e)
            {   
                exit(1);
            }
            return(0);
    }
    else{
        if(!strcmp(instructions[0],"cd")){ // cd en build-in
            if(cd(instructions[1])){ //on suppose ici que cd à toujours un unique argument argument
                
                return(1);
            } 
            else{
                if (option_e){   
                    exit(1);
                }
                return(0);
            }
        }
        else{
            if(!strcmp(instructions[0],"fg")){// commande fg
                if(instructions[1] != NULL){ // Cas ou fg est lancé avec un argument
                    int code = fg(atoi(instructions[1]));
                    printf("[%s->%d]\n",instructions[1],code);
                    fflush(stdout);
                    return(code);
                }
                else{
                    int i = 0;
                    while(background[i] != '\0')
                    {
                        int aux = waitpid(background[i],&status,WNOHANG);
                        if( (aux == 0) || (background[i] == aux)){
                            int code = fg(background[i]);
                            printf("[%d->%d]\n",background[i],WEXITSTATUS(status));
                            return(code);
                        }                       
                        i++;
                    }
                    return(1);
                }
            }
        }
    }
    if(pipe(fd)<0){
        perror("pipe");
        close(old_fd);
        return(-1);
    }
    if ((fk = fork()) < 0)
    {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        close(old_fd);
        return(-1);
    }

    if (fk == 0) { // On lit l'entrée par le processus old_fd
        close(fd[0]);
        dup2(old_fd,0);
        close(old_fd);
        if(display == 0){ 
            dup2(fd[1],1);
            close(fd[1]);
        }
        else{
            if(display > 2){
                dup2(display,1);
                close(display);
                close(fd[1]);
            }
            else{
                close(fd[1]);
            }
        }
        code = execvp(instructions[0],instructions); // le fils exécute la commande restante
        if (code != 0) { // Problème dans l’appel système
            if (option_e)
            {   
                exit(2); // on interromp le fils et le père (status = 2)
            }
            exit(1); // on interromp le fils seulement, le programme continue
        }
    } 
    else {
        if(display != 0){
            waitpid(fk,&status,0);
            if(display > 2){
                close(display);
            }
            if(WEXITSTATUS(status)==0){ // commande a fonctionné correctement
                close(old_fd);
                close(fd[1]);
                if(display == 0){ // cas ou on renvoie la sortie de la commande
                    return(fd[0]); // On renvoie le descripteur
                }
                else{
                    if (display == 1) // cas ou on affiche la sortie de la commande
                    {
                        close(fd[0]);
                        return(1); // on affiche le résultat et renvoie valeur de retour ok
                    }
                }
            }
            else{
                if(WEXITSTATUS(status)==2){ // cas ou est en mode -e
                    close(old_fd);
                    close(fd[1]);
                    close(fd[0]);
                    exit(1);
                }
                else{
                    if(WEXITSTATUS(status)==1){ // on a détecté une erreur sans option -e
                        close(old_fd);
                        close(fd[1]);
                        close(fd[0]);
                        return(0);
                    }
                }
            }
        }
        close(old_fd);
        close(fd[1]);
        return(fd[0]);
    }
    return 1;
}

int instruction_redi(char** instructions, int old_fd){ // Pour les commandes contenant : | > >> <
    int i = 0;
    char* commande[MAX] = {};
    while((instructions[i]!=NULL) && strcmp(instructions[i],">") && strcmp(instructions[i],">>") && strcmp(instructions[i],"<") && strcmp(instructions[i],"|")){
        commande[i] = instructions[i];
        i++;
    }
    if(instructions[i] == NULL){// On est à la fin de la commande
        int descrip = execution(commande,old_fd,1);
        return(descrip);
    }
    else{ // On est pas à la fin de la commande
        int aux = 0;
        char* fin[MAX] = {};
        if (!strcmp(instructions[i],"<"))
        {
            int descrip = open(instructions[i+1],O_RDONLY|O_CREAT,0666);
            i=i+2;
            if(instructions[i] == NULL){ // cas d'une commande style cat < test.txt
                return(execution(commande,descrip,1));
            }
            else{
                old_fd = execution(commande,descrip,0);
            }
        }
        while(instructions[i+1+aux]!=NULL)
        {
            fin[aux] = instructions[i+1+aux];
            aux++;
        }
        fin[aux] = NULL; // fin de la commande copiée
        if (!strcmp(instructions[i],"|"))
        {
            return(instruction_redi(fin,execution(commande,old_fd,0)));
           
        }
        else{
            if(!strcmp(instructions[i],">")){
                int ecraser = open(instructions[i+1],O_CREAT|O_TRUNC|O_WRONLY,0666);
                return(execution(commande,old_fd,ecraser));
            }
            else{
                if(!strcmp(instructions[i],">>")){
                    int ecraser = open(instructions[i+1],O_CREAT|O_APPEND|O_WRONLY,0666);
                    return(execution(commande,old_fd,ecraser));
                }
            }
        } 
    }
    return(1);
}

int instruction_simple(char** instructions,char* symbole, int state){ // Pour les commandes contenant && ; ||
    //Si commande de la forme a ; b && c || d --> deb = {"a"} ; fin = {"b && c || d"} ; symbole = symbole de l'appel précédent ; state = vrai ou faux 
    int i = 0;
    char* debut[MAX] = {};
    char* fin[MAX] = {};
    while(instructions[i]!=NULL && strcmp(instructions[i],"&&") && strcmp(instructions[i],";") && strcmp(instructions[i],"||")){
        debut[i] = instructions[i];
        i++;
    }
    if(instructions[i]!=NULL){ // reste des instructions derrière
        int aux = 0;
        while(instructions[i+1+aux]!=NULL)
        {
            fin[aux] = instructions[i+1+aux];
            aux++;
        }
        fin[aux] = NULL;
        if (!strcmp(symbole,";"))
        {   
            return(instruction_simple(fin,instructions[i],instruction_simple(debut,symbole,state)));
        }
        else{
            if (!strcmp(symbole,"&&"))
            {
                if(state){ // de la forme true && deb instruction[i] fin
                    return(instruction_simple(fin,instructions[i],instruction_simple(debut,symbole,state)));
                }
                else{ // de la forme false && deb instruction[i] fin
                    return(instruction_simple(fin,instructions[i],0));
                }
            }
            else{
                if (!strcmp(symbole,"||")){
                    if(!state){ // de la forme false || deb instruction[i] fin
                        return(instruction_simple(fin,instructions[i],instruction_simple(debut,symbole,state)));
                    }
                    else{ // de la forme true || deb instruction[i] fin
                        return(instruction_simple(fin,instructions[i],1));
                    }
                }
            }  
        } 
    }
    else{ // On a ici une commande à appliquer : par exemple echo Alexandre ou ls
        if (!strcmp(";",symbole)){
            return(instruction_redi(instructions,dup(0)));
        }
        else{
            if (!strcmp("&&",symbole)){
                if(state){
                    return(instruction_redi(instructions,dup(0)));
                }
                else{
                    return(0);
                }
            }
            else{
                if (!strcmp("||",symbole)){
                    if(!state){
                        return(instruction_redi(instructions,dup(0)));
                    }
                }
                else{
                    return(1);
                }
                
            }
        }
        
    }
    return 0;
}


int parenthese(char** instructions, char* symbole, int state){ // Pour les commandes parenthèsées avec && ; ||
    int a = 0;
    int condi = 0;
    while (instructions[a] != NULL){
        if (!strcmp(instructions[a],"("))
        {
            condi++;
        }
        if (!strcmp(instructions[a],")"))
        {
            condi++;
        }
        a++;
    }
    if(!condi){
        return(instruction_simple(instructions,symbole,state));
    }
    // On va ici chercher à déterminer si il y a des parenthèses dans la commande
    //printf("\n");
    //print(instructions);
    //printf("\n");
    int ouvrante = 0;
    int fermante = 0;
    int k = 0;
    while (instructions[k] != NULL &&((strcmp(instructions[k],";")!=0 && strcmp(instructions[k],"&&")!=0 && strcmp(instructions[k],"||")!=0) || ouvrante !=fermante)){
        if (!strcmp(instructions[k],"("))
        {
            ouvrante++;
        }
        if (!strcmp(instructions[k],")"))
        {
            fermante++;
        }
        k++;
    }
    char* debut[MAX]; // même principe que pour la fonction instruction simple
    char* fin[MAX];
    int aux;
    if(ouvrante==0){
        aux = 0;
    }
    else{
        aux = 1;
    }
    for(int i = aux; i < k-aux; i++)
    {
        debut[i-aux] = instructions[i];
    }
    //printf("DEBUT\n");
    //print(debut);
    int i = 0;
    while(instructions[k+ i +1]!=NULL)
    {
        fin[i] = instructions[k+i+1];
        i++;
    }
    ///printf("FIN\n");
    //print(fin);
    fin[i] = NULL;

    if (instructions[k] != NULL) // Il reste encore une instruction à la fin
    {   
        if (!strcmp(symbole,";"))
        {   
            return(parenthese(fin,instructions[k],parenthese(debut,symbole,state)));
        }
        else{
            if (!strcmp(symbole,"&&"))
            {
                if(state){ // de la forme true && deb instruction[i] fin
                    return(parenthese(fin,instructions[k],parenthese(debut,symbole,state)));
                }
                else{ // de la forme false && deb instruction[i] fin
                    return(parenthese(fin,instructions[k],0));
                }
            }
            else{
                if (!strcmp(symbole,"||")){
                    if(!state){ // de la forme false || deb instruction[i] fin
                        return(parenthese(fin,instructions[k],parenthese(debut,symbole,state)));
                    }
                    else{ // de la forme true || deb instruction[i] fin
                        return(parenthese(fin,instructions[k],1));
                    }
                }
            }
        }
    }
    else{ // On a ici une commande à appliquer : par exemple echo Alexandre ou ls
        if (!strcmp(";",symbole)){
            return(parenthese(debut,symbole,state));
        }
        else{
            if (!strcmp("&&",symbole)){
                if(state){
                    return(parenthese(debut,symbole,state));
                }
                else{
                    return(0);
                }
            }
            else{
                if (!strcmp("||",symbole)){
                    if(!state){
                        return(parenthese(debut,symbole,state));
                    }
                }
                else{
                    return(1);
                }
                
            }
        }
        
    }
    return 0;
}

int instruction_bg(char** instructions){
    int i = 0;
    char* debut[MAX];
    char* fin[MAX];
    while(instructions[i]!=NULL && strcmp(instructions[i],"&")){
        debut[i] = instructions[i];
        i++;
    }
    if(instructions[i] == NULL){
        return(parenthese(instructions,";",1)); // fin d'une commande classique
    }
    else{ // forme & avec commande derrière
        if(instructions[i+1]==NULL){ // forme & fin de commande 
            pid_t fk;
            if ((fk = fork()) == 0) 
            {
                //printf("[%d]\n",getpid());
                //fflush(stdout);
                int aux = parenthese(debut,";",1);
                if(aux){ // code de retour de exec pas compatible avec posix
                    aux = 0;
                }
                else{
                    aux = 1;
                }
                //printf("[%d->%d]\n",getpid(),aux);
                exit(aux);
            }
            else{
                printf("[%d]\n",fk);
                fflush(stdout);
                add_bg(fk);
                //last_bg = fk;
                return(1);
            }
        }
        
        int aux = 0;
        while(instructions[i+1+aux]!=NULL)
        {
            fin[aux] = instructions[i+1+aux];
            aux++;
        }
        fin[aux] = NULL;
        pid_t fk;
        if ((fk = fork()) == 0) 
        {
            //printf("[%d]\n",getpid());
            //fflush(stdout);
            int aux = parenthese(debut,";",1);
            if(aux){ // code de retour de exec pas compatible avec posix
                aux = 0;
            }
            else{
                aux = 1;
            }
            //printf("[%d->%d]\n",getpid(),aux);
            exit(aux);
        }
        else{
            printf("[%d]\n",fk);
            fflush(stdout);
            add_bg(fk);
            //last_bg = fk;
            return(instruction_bg(fin));
        }
    }
}

void parseur(char * commande){ // Sépare une commande string en table de string (séparation via les espaces)
    char* instructions[MAX];
    int n = 0;
    for (instructions[n] = strtok(commande, " "); instructions[n]; instructions[n] = strtok(NULL, " "))
    {
        //printf("%s\n", instructions[n]);
        n++;
    }
    //parenthese(instructions,";",1);
    instruction_bg(instructions);
    return;
}

void non_interactif_fichier(const char* nom_fichier){
    int descrip = open(nom_fichier,O_RDONLY|O_CREAT,0666);
    char cmd[MAX];
    read(descrip,cmd,MAX);
    char* instructions[MAX];
    int n = 0;
    for (instructions[n] = strtok(cmd,"\n"); instructions[n]; instructions[n] = strtok(NULL, "\n"))
    {
        n++;
    }
    for (int i = 0; i < n; i++)
    {
        parseur(instructions[i]);
    }
}

void non_interactif_no_shell(){
    char cmd[MAX];
    read(0,cmd,MAX);
    char* instructions[MAX];
    int n = 0;
    for (instructions[n] = strtok(cmd,"\n"); instructions[n]; instructions[n] = strtok(NULL, "\n"))
    {
        n++;
    }
    for (int i = 0; i < n; i++)
    {
        parseur(instructions[i]);
    }
}

void mode_shell(){
    char* user =getenv("USER");
    char hostName[MAX];
    gethostname(hostName, sizeof(hostName));
    char instruction[MAX];
    char* aux;
    while(strcmp(instruction,"exit")){
        instruction[0]='\0';
        fflush(stdout);
        if(option_r){
            char nom_shell[MAX] = "";
            strcat(nom_shell,user);
            strcat(nom_shell,"@");
            strcat(nom_shell,hostName);
            strcat(nom_shell,":");
            strcat(nom_shell,cwd);
            strcat(nom_shell,"$ ");
            aux = readline(nom_shell);
            if(aux == NULL){
                printf("\n");
                return;
            }
            if(strcmp(aux,"")){
                strcpy(instruction,aux);
                add_history(instruction);
                parseur(instruction);
            }
        }
        else{
            char nom_shell[MAX] = "";
            strcat(nom_shell,user);
            strcat(nom_shell,"@");
            strcat(nom_shell,hostName);
            strcat(nom_shell,":");
            strcat(nom_shell,cwd);
            strcat(nom_shell,"$ ");
            printf("%s",nom_shell);
            //memset(instruction, 0, MAX);
            fgets(instruction,MAX,stdin);
            if(feof(stdin)){
                printf("\n");
                return;
            }
            if(strcmp(instruction,"\n")){
                instruction[strlen(instruction)-1] = 0; //On se débarrasse du retour chariot à la fin
                parseur(instruction);
            }
        }
    }
}

int main(int argc, char const *argv[])
{   
    for (int i = 1; i < argc; i++){
            if (!strcmp(argv[i],"-e"))
            {
                option_e = 1; //printf("il y a l'option -e\n");
            }
            else if(!strcmp(argv[i],"-r"))
            {
                option_r = 1; //printf("il y a l'option -r\n");
                // Chargement dynamique de readline
                // ldconfig -p | grep readline.so.8 | tr ' ' '\n' | grep /  (Pour obtenir le path de readline)
                char* cmd_0[MAX] = {"ldconfig","-p",NULL};
                int fd = execution(cmd_0,dup(0),0);
                char* cmd_1[MAX] = {"grep","readline.so.8",NULL};
                fd = execution(cmd_1,fd,0);
                char* cmd_2[MAX] = {"tr","' '","'\n'",NULL};
                fd = execution(cmd_2,fd,0);
                char* cmd_3[MAX] = {"grep","/",NULL};
                fd = execution(cmd_3,fd,0);
                char path[MAX];
                //affichage(fd,dup(1));
                read(fd,path,MAX);
                path[strlen(path)-1] = 0; //On se débarrasse du retour chariot à la fin
                close(fd);
                //printf("%s\n",path);
                //void* readline_handle = dlopen (path, RTLD_LAZY);
                void* readline_handle = dlopen ("libreadline.so", RTLD_LAZY);
                readline = dlsym (readline_handle, "readline");
                add_history = dlsym(readline_handle, "add_history");
            }
    }
    if (getcwd(cwd, sizeof(cwd)) == NULL)
         perror("getcwd() error");
    /*else
       printf(" current working directory is: %s\n", cwd);*/
    if (getcwd(pwd, sizeof(pwd)) == NULL)
         perror("getcwd() error");
    /*else
       // printf(" previous working directory is: %s\n", pwd);*/
    if(isatty(fileno(stdin))){ // Entrée depuis un terminal
        if((argc>1 && !option_e && !option_r) || (argc>2 && option_e && !option_r) || (argc>2 && option_r && !option_e)){ // mode non_interactif, entrée de fichier contenant des commandes
            for (int i = 1; i < argc; i++){
                if (strcmp(argv[i],"-e") || strcmp(argv[i],"-r")){
                    non_interactif_fichier(argv[i]); // On suppose que c'est nécéssairement un fichier
                }
            }
            return 0;
        }
        else{ // on est en mode prompt
            mode_shell();
        }
    }
    else{ // Entrée standart n'est pas un terminal
        if((argc>1 && !option_e && !option_r) || (argc>2 && option_e && !option_r) || (argc>2 && option_r && !option_e)){ // mode non_interactif, entrée de fichier contenant des commandes
            for (int i = 1; i < argc; i++){
                if (strcmp(argv[i],"-e") || strcmp(argv[i],"-r")){
                    non_interactif_fichier(argv[i]); // On suppose que c'est nécéssairement un fichier
                }
            }
        }
        else{
            if(option_r){
                mode_shell();
            }
            else{
                non_interactif_no_shell();
            }
        }
    }
   return 0;
}